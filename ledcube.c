#include <util/delay.h>
#include <avr/sleep.h>
#include <stdlib.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer1.h>
#include <arduino/sleep.h>


#define PIN_SCLK 8
#define PIN_SIN  9
#define PIN_XLAT 10
#define PIN_BLANK 11

/* 12-bit output values used for different pixel values. */
uint16_t pixel2out_high[16] =
{ 0, 0, 0, 0, 0,  0,  0,  0,  0,   0,   0, 445>>8,  775>>8,  1350>>8,  2352>>8,  4095>>8 };
uint16_t pixel2out_low[16] =
{ 0, 1, 3, 5, 9, 15, 27, 48, 84, 147, 255, 445&255, 775&255, 1350&255, 2352&255, 4095&255 };

#define NUM_LEDS 1331
#define NUM_LAYERS 11
#define LEDS_PER_LAYER 121
#define BITS_PER_LED 4
#define DATA_SIZE ((NUM_LEDS*BITS_PER_LED+7)/8)
#define FRAME_SIZE (DATA_SIZE + 6)
#define NUM_FRAMES 2

static uint8_t frames[NUM_FRAMES][FRAME_SIZE];

/* Keep track of port B state. */
/* ToDo: remove volatile once we don't need it in interrupt routines anymore. */
static volatile uint8_t portb_state;
static void
my_pin13_high(void)
{
  portb_state |= (1<<5);
  pin13_high();
}
static void
my_pin13_low(void)
{
  portb_state &= ~(1<<5);
  pin13_low();
}

/* Serial reception. */

/*
  Frame format:
  <0> <frame counter 0-63> <len_low> <len_high> <data> <checksum> <0xff>
*/

/*
  The last frame fully loaded, to be shifted out to the cube by the timer
  interrupt.

  Declared volatile, as reads must not be cached - the serial interrupt
  handler modifies it asynchroneously.
*/
static volatile uint8_t show_frame = 0;
/* The frame currently being received over serial. */
static uint8_t current_frame = 0;
/* The count of bytes already received in current frame. */
static uint16_t current_idx = 0;
/* Count of bytes (<=255) to receive using the "fast path" code. */
static uint8_t remain_bytes= 0;
/* Position to write next byte received in the "fast path" code. */
static uint8_t *cur_data_ptr;

/*
  Do the less often run serial stuff here, to keep the most serial interrupts
  lean-and-mean (avoid having to push tons of registers in the fast case).

  Idea is to code this really minimal, and then do it in hand-crafted
  assembler.

  If we do it in C, the compiler pushes a whole lot of regs (the caller-save
  ones) due to the nested call, and it does this always. But in asm, we can
  avoid saving all the regs in the fast path.
*/

/*
serial_interrupt_rx()
{
  int8_t r = remain_bytes;
  if (r > 0)
  {
    remain_bytes= r-1;
    *cur_data_ptr++ = serial_read();
  }
  else
    serial_interrupt_slow_part();
}
*/

static void serial_interrupt_slow_part(void) __attribute__((noinline));
serial_interrupt_rx_naked()
{
//  ++remain_bytes;
  asm volatile(
    "push r0\n\t"
    "in   r0, 0x3f\n\t"
    "push r0\n\t"
    "push r30\n\t"
    "push r31\n\t"

    "lds  r30, remain_bytes\n\t"
    "subi r30, 1\n\t"
    "brcs 1f\n\t"              /* if remain_bytes was 0, do it the slow way */
    "sts  remain_bytes, r30\n\t"
    "lds  r30, cur_data_ptr\n\t"
    "lds  r31, cur_data_ptr+1\n\t"
    "lds  r0,198\n\t"                           /* serial_read() */
    "st   Z+, r0\n\t"
    "sts  cur_data_ptr, r30\n\t"
    "sts  cur_data_ptr+1, r31\n\t"

    "pop  r31\n\t"
    "pop  r30\n\t"
    "pop  r0\n\t"
    "out  0x3f, r0\n\t"
    "pop  r0\n\t"
    "reti\n"

    "1:\n\t"
    "push r1\n\t"
    "clr  r1\n\t"
    "push r18\n\t"
    "push r19\n\t"
    "push r20\n\t"
    "push r21\n\t"
    "push r22\n\t"
    "push r23\n\t"
    "push r24\n\t"
    "push r25\n\t"
    "push r26\n\t"
    "push r27\n\t"
    "call serial_interrupt_slow_part\n\t"
    "pop  r27\n\t"
    "pop  r26\n\t"
    "pop  r25\n\t"
    "pop  r24\n\t"
    "pop  r23\n\t"
    "pop  r22\n\t"
    "pop  r21\n\t"
    "pop  r20\n\t"
    "pop  r19\n\t"
    "pop  r18\n\t"
    "pop  r1\n\t"
    "pop  r31\n\t"
    "pop  r30\n\t"
    "pop  r0\n\t"
    "out  0x3f, r0\n\t"
    "pop  r0\n\t"
    "reti\n");

  /*
    Not reached - we return from interrupt inside the asm.
    But we have to refer to the function, or it will be optimised out.
  */
  serial_interrupt_slow_part();
}

static volatile uint8_t frame_receive_counter= 0;
static void
serial_interrupt_slow_part(void)
{
  uint8_t c;
  uint8_t cur = current_frame;

  c = serial_read();
  frames[cur][current_idx] = c;
  current_idx++;

  if (current_idx >= 4 && current_idx < FRAME_SIZE-2)
  {
    /* Use the fast path for the next up-to-255 data bytes. */
    uint16_t total= (FRAME_SIZE-2) - current_idx;
    remain_bytes= total > 255 ? 255 : total;
    cur_data_ptr= &frames[cur][current_idx];
    current_idx+= remain_bytes;
    return;
  }
  else if (current_idx >= FRAME_SIZE)
  {
    ++frame_receive_counter;
    current_idx = 0;
    show_frame = cur;
    current_frame = (cur + 1) % NUM_FRAMES;
  }
}

static void
serial_reset(void)
{
  cli();
  current_idx = 0;
  remain_bytes = 0;
  sei();
}

/*
  Shift out 12 bits to the LED controller (a single LED).
  ASM optimised to get down to 5 cycles / bit, should be ~65 cycles including
  setup overhead.

  Idea is
   - Use bst/bld in unrolled loop to quickly move each data bit into the SIN
     bit position.
   - Use `out' instruction (1 cycle vs. 2 cycles + conditional jump cost for
     sbi/cbi) for speed, and to set clock low in same cycle as setting data
     bit. This requires that all other used bits in this port have known
     fixed values.
*/
static inline void
shift_out_12bit(uint8_t bstate, uint8_t val_high, uint8_t val_low)
{
  /*
    SCLK is bit 0 in port B [8].
    SIN is bit 1 in port B [9].

    PORTB is register 5.

    SCLK is assumed low at the start.
  */
  asm volatile(
    "\t"
    "bst %[reg_val_high], 3\n\t"  /* Grab the first (MSB) data bit */

    "bld %[reg_port], 1\n\t"      /* Store data bit in SIN position */
    "out 5, %[reg_port]\n\t"      /* Output data bit on SIN, set SCLK low */
    "bst %[reg_val_high], 2\n\t"  /* Grab the next data bit */
    "sbi 5, 0\n\t"                /* Set SCLK high to send first data bit */

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_high], 1\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_high], 0\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 7\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 6\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 5\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 4\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 3\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 2\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 1\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "bst %[reg_val_low], 0\n\t"
    "sbi 5, 0\n\t"

    "bld %[reg_port], 1\n\t"
    "out 5, %[reg_port]\n\t"
    "nop\n\t"
    "sbi 5, 0\n\t"

    "nop\n\t"
    "cbi 5, 0\n\t"

    : [reg_port] "+r" (bstate)
    : [reg_val_high] "r" (val_high), [reg_val_low] "r" (val_low)
  );
}

static uint8_t old_frame= 0xff;
static uint8_t cur_layer= NUM_LAYERS;
static volatile uint8_t frame_refresh_counter= 0;
timer1_interrupt_a()
{
  uint8_t i;
  uint8_t bstate;
  static uint8_t *data;

  sei();

  /* First, switch layer, so we get a stable timing for this important step. */
  /* But skip the very first time, when we have no data yet. */
  if (cur_layer < NUM_LAYERS)
  {
    pin_high(PIN_BLANK);
    pin_high(PIN_XLAT);
    pin_low(PIN_XLAT);

    /* Switch the MOSFETs to the next layer. */
    switch (cur_layer)
    {
    case 0: pinA4_high(); pin2_low(); break;
    case 1: pin2_high(); pin3_low(); break;
    case 2: pin3_high(); pin4_low(); break;
    case 3: pin4_high(); pin5_low(); break;
    case 4: pin5_high(); pin6_low(); break;
    case 5: pin6_high(); pin7_low(); break;
    case 6: pin7_high(); pinA0_low(); break;
    case 7: pinA0_high(); pinA1_low(); break;
    case 8: pinA1_high(); pinA2_low(); break;
    case 9: pinA2_high(); pinA3_low(); break;
    case 10:pinA3_high(); pinA4_low(); break;
    }
    /* ToDo: Hack for now: only display lowest layers. */
    if (cur_layer <= 11)
    {
      pin_low(PIN_BLANK);
      portb_state &= 0xf7;  /* fix BLANK low. */
    }
    else
      portb_state |= 0x08;  /* fix BLANK high. */
  }

  cur_layer++;
  if (cur_layer >= NUM_LAYERS)
    cur_layer= 0;

  if (cur_layer == 0) {
    ++frame_refresh_counter;
    /* Check for a new frame. */
    uint8_t cur_frame = show_frame;
    if (cur_frame != old_frame)
    {
      serial_write(frames[cur_frame][1]);
      old_frame= cur_frame;
    }
    data= &frames[cur_frame][4];
  }

  /* Now shift out one layer. */
  bstate = portb_state & 0xf8;  /* XLAT, XCLK both 0 */
  if ((LEDS_PER_LAYER % 2) && (cur_layer % 2))
  {
    uint8_t pixel = *data++ & 0xf;
    shift_out_12bit(bstate, pixel2out_high[pixel], pixel2out_low[pixel]);
  }
  for (i = 0; i < LEDS_PER_LAYER/2; i++)
  {
    uint8_t v = *data++;
    uint8_t first = v >> 4;
    uint8_t second = v & 0xf;
    shift_out_12bit(bstate, pixel2out_high[first], pixel2out_low[first]);
    shift_out_12bit(bstate, pixel2out_high[second], pixel2out_low[second]);
  }
  if ((LEDS_PER_LAYER % 2) && !(cur_layer % 2))
  {
    uint8_t pixel = *data >> 4;
    shift_out_12bit(bstate, pixel2out_high[pixel], pixel2out_low[pixel]);
  }
}


static void
init(void) {
  pin_mode_output(PIN_SCLK);
  pin_low(PIN_SCLK);
  pin_mode_output(PIN_SIN);
  pin_low(PIN_SIN);
  pin_mode_output(PIN_XLAT);
  pin_low(PIN_XLAT);
  pin_mode_output(PIN_BLANK);
  pin_high(PIN_BLANK);    /* All leds are off initially */

  pin_high(2);
  pin_mode_output(2);
  pin_high(3);
  pin_mode_output(3);
  pin_high(4);
  pin_mode_output(4);
  pin_high(5);
  pin_mode_output(5);
  pin_high(6);
  pin_mode_output(6);
  pin_high(7);
  pin_mode_output(7);
  pin_high(A0);
  pin_mode_output(A0);
  pin_high(A1);
  pin_mode_output(A1);
  pin_high(A2);
  pin_mode_output(A2);
  pin_high(A3);
  pin_mode_output(A3);
  pin_high(A4);
  pin_mode_output(A4);

//  serial_baud_9600();
//  serial_baud_115200();
//  serial_baud_230400();
//  serial_baud_250k();
  serial_baud_500k();
  serial_mode_8n1();
  serial_transmitter_enable();
  serial_receiver_enable();
  serial_interrupt_rx_enable();

  /* Use the 16-bit timer1 at the full 16MHz resolution. */
  timer1_count_set(0);
  timer1_clock_d1();
  timer1_mode_ctc();
  timer1_compare_a_set(17778);   /* 16MHz / 177778 -> 900Hz. */
  //timer1_compare_a_set(35955);   /* 16MHz / 35955 -> 445Hz. */
  timer1_interrupt_a_enable();
}

static void anim_solid(uint8_t f, uint8_t val);
static void cornercube_5(uint8_t f);

int
main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
  uint8_t previous_refresh_counter;
  uint8_t generate_frame = 0;
  uint8_t cur_refresh_counter;
  uint8_t onboard_animation = 0;
  uint8_t cur_receive_counter;
  uint8_t previous_receive_counter = 0;
  uint8_t previous_receive_timestamp = 0;
  uint8_t generate_counter= 0;

  init();
  sei();

  pin13_mode_output();
  my_pin13_low();

  sleep_mode_idle();
  for (;;)
  {
    if (onboard_animation)
      my_pin13_high();
    else
      my_pin13_low();
    if (onboard_animation)
      show_frame = generate_frame;
    previous_refresh_counter= frame_refresh_counter;
    /* Wait for refresh handler to start on a new frame. */
    while ((cur_refresh_counter = frame_refresh_counter) == previous_refresh_counter)
      ;
    if (onboard_animation)
      generate_frame = (generate_frame + 1) % NUM_FRAMES;

    cur_receive_counter = frame_receive_counter;
    if (cur_receive_counter != previous_receive_counter)
    {
      /* We received a new frame on serial. */
      onboard_animation= 0;
      previous_receive_counter = cur_receive_counter;
      previous_receive_timestamp = cur_refresh_counter;
    }
    else if (!onboard_animation &&
             (uint8_t)(cur_refresh_counter - previous_receive_timestamp) > 30)
    {
      /*
        Nothing received on serial for a while.
        Switch to on-board animation, and reset the serial state so that it
        will recover when communication resumes.
      */
      serial_reset();
      onboard_animation = 1;
      generate_frame = (show_frame + (NUM_FRAMES - 1)) % NUM_FRAMES;
    }

    if (onboard_animation)
    {
//      anim_solid(generate_frame, 15);
      cornercube_5(generate_frame);
    }
    ++generate_counter;
  }
}

static void
fast_clear(uint8_t frame, uint8_t val)
{
  uint8_t v = (uint8_t)0x11 * (val & 0xf);
  uint8_t *p= &frames[frame][4];
  for (uint8_t i= 0; i < DATA_SIZE/16; i++)
  {
    *p++= v; *p++= v; *p++= v; *p++= v; *p++= v; *p++= v; *p++= v; *p++= v;
    *p++= v; *p++= v; *p++= v; *p++= v; *p++= v; *p++= v; *p++= v; *p++= v;
  }
  for (uint8_t i= 0; i < DATA_SIZE % 16; i++)
    *p++= 0;
}

static void
pixel5(uint8_t f, uint8_t x, uint8_t y, uint8_t z, uint8_t val)
{
  uint8_t *p;
  uint8_t v;
  int idx= LEDS_PER_LAYER * z;
  idx += LEDS_PER_LAYER - 25;
  idx += (4-x);
  idx += (4-y)*5;

  p = &frames[f][4] + idx/2;
  v = *p;
  if (idx % 2)
    *p = (v & 0xf0) | (val & 0xf);
  else
    *p = (v & 0x0f) | (val & 0xf)<<4;
}

#define RAND_N(n) (rand()/(RAND_MAX/(n)+1))

/* Animation: corner-cube */

struct cornercube_data {
  int base_x, base_y, base_z;
  int dir_x, dir_y, dir_z;
  int col, target_col;
  int corner;
};

static struct cornercube_data ccd;
static int cc_frame= -1;

static void
cornercube_5(uint8_t f)
{
  static const int base_count= 23;

  fast_clear(f, 0);
  if (cc_frame < 0)
  {
    // Initialise first corner to expand from
    ccd.col= 0;
    ccd.target_col= 11 + RAND_N(5);
    ccd.base_x= 0;
    ccd.base_y= 0;
    ccd.base_z= 0;
    ccd.dir_x= 1;
    ccd.dir_y= 1;
    ccd.dir_z= 1;
    ccd.corner= 0;
    cc_frame = 0;
  }

  if ((cc_frame / (base_count+1))%2 == 0)
  {
    // Cube expanding from corner.
    if ((cc_frame % (base_count+1)) == 0)
    {
      ccd.col= ccd.target_col;
      ccd.target_col= 11 + RAND_N(5);
    }
    float expand_factor= (float)(cc_frame % (base_count+1)) / base_count;
    int col= (int)((float)ccd.col +
                   expand_factor * ((float)ccd.target_col - (float)ccd.col) + 0.5);
    int side_len= (int)(expand_factor * 4 + 0.5);
    for (int i= 0; i <= side_len; i++)
    {
      int end_x= ccd.base_x+ccd.dir_x*side_len;
      int end_y= ccd.base_y+ccd.dir_y*side_len;
      int end_z= ccd.base_z+ccd.dir_z*side_len;
      pixel5(f,ccd.base_x + i*ccd.dir_x, ccd.base_y, ccd.base_z, col);
      pixel5(f, ccd.base_x + i*ccd.dir_x, end_y, ccd.base_z, col);
      pixel5(f, ccd.base_x + i*ccd.dir_x, ccd.base_y, end_z, col);
      pixel5(f, ccd.base_x + i*ccd.dir_x, end_y, end_z, col);
      pixel5(f, ccd.base_x, ccd.base_y + i*ccd.dir_y, ccd.base_z, col);
      pixel5(f, end_x, ccd.base_y + i*ccd.dir_y, ccd.base_z, col);
      pixel5(f, ccd.base_x, ccd.base_y + i*ccd.dir_y, end_z, col);
      pixel5(f, end_x, ccd.base_y + i*ccd.dir_y, end_z, col);
      pixel5(f, ccd.base_x, ccd.base_y, ccd.base_z + i*ccd.dir_z, col);
      pixel5(f, end_x, ccd.base_y, ccd.base_z + i*ccd.dir_z, col);
      pixel5(f, ccd.base_x, end_y, ccd.base_z + i*ccd.dir_z, col);
      pixel5(f, end_x, end_y, ccd.base_z + i*ccd.dir_z, col);
    }
  }
  else
  {
    // Cube retreating to other corner.
    if ((cc_frame % (base_count+1)) == 0)
    {
      // At first frame, select new base point.
      int corner;
      do
        corner= RAND_N(8);
      while (corner == ccd.corner);
      ccd.corner= corner;
      if (corner & 1)
      {
        ccd.base_x= 4;
        ccd.dir_x= -1;
      }
      else
      {
        ccd.base_x= 0;
        ccd.dir_x= 1;
      }
      if (corner & 2)
      {
        ccd.base_y= 4;
        ccd.dir_y= -1;
      }
      else
      {
        ccd.base_y= 0;
        ccd.dir_y= 1;
      }
      if (corner & 4)
      {
        ccd.base_z= 4;
        ccd.dir_z= -1;
      }
      else
      {
        ccd.base_z= 0;
        ccd.dir_z= 1;
      }
      // And a new target colour.
      ccd.col= ccd.target_col;
      ccd.target_col= 11 + RAND_N(5);
    }
    float expand_factor= (float)(base_count - (cc_frame % (base_count + 1))) / base_count;
    int col= (int)((float)ccd.col +
                   expand_factor * ((float)ccd.target_col - (float)ccd.col) + 0.5);
    int side_len= (int)(expand_factor * 4 + 0.5);
    for (int i= 0; i <= side_len; i++)
    {
      int end_x= ccd.base_x+ccd.dir_x*side_len;
      int end_y= ccd.base_y+ccd.dir_y*side_len;
      int end_z= ccd.base_z+ccd.dir_z*side_len;
      pixel5(f, ccd.base_x + i*ccd.dir_x, ccd.base_y, ccd.base_z, col);
      pixel5(f, ccd.base_x + i*ccd.dir_x, end_y, ccd.base_z, col);
      pixel5(f, ccd.base_x + i*ccd.dir_x, ccd.base_y, end_z, col);
      pixel5(f, ccd.base_x + i*ccd.dir_x, end_y, end_z, col);
      pixel5(f, ccd.base_x, ccd.base_y + i*ccd.dir_y, ccd.base_z, col);
      pixel5(f, end_x, ccd.base_y + i*ccd.dir_y, ccd.base_z, col);
      pixel5(f, ccd.base_x, ccd.base_y + i*ccd.dir_y, end_z, col);
      pixel5(f, end_x, ccd.base_y + i*ccd.dir_y, end_z, col);
      pixel5(f, ccd.base_x, ccd.base_y, ccd.base_z + i*ccd.dir_z, col);
      pixel5(f, end_x, ccd.base_y, ccd.base_z + i*ccd.dir_z, col);
      pixel5(f, ccd.base_x, end_y, ccd.base_z + i*ccd.dir_z, col);
      pixel5(f, end_x, end_y, ccd.base_z + i*ccd.dir_z, col);
    }
  }
  ++cc_frame;
  if (cc_frame >= 2*(base_count+1))
    cc_frame= 0;
}

static void
anim_solid(uint8_t f, uint8_t val)
{
  fast_clear(f, val);
}
