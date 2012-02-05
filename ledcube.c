#include <util/delay.h>
#include <avr/sleep.h>

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
static volatile uint8_t show_frame = 0;
serial_interrupt_rx()
{
  uint8_t c;
  static uint8_t current_frame = 0;
  static uint16_t current_idx = 0;
  uint8_t cur = current_frame;

   if (cur % 2)
     my_pin13_low();
   else
    my_pin13_high();

  c = serial_read();
  frames[cur][current_idx] = c;
  current_idx++;
  if (current_idx >= FRAME_SIZE)
  {
    current_idx = 0;
    show_frame = cur;
    current_frame = (cur + 1) % NUM_FRAMES;
  }
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
    case 0: pinA5_low(); pin2_high();
    case 1: pin2_low(); pin3_high();
    case 2: pin3_low(); pin4_high();
    case 3: pin4_low(); pin5_high();
    case 4: pin5_low(); pin6_high();
    case 5: pin6_low(); pin7_high();
    case 6: pin7_low(); pinA0_high();
    case 7: pinA0_low(); pinA1_high();
    case 8: pinA1_low(); pinA2_high();
    case 9: pinA2_low(); pinA3_high();
    case 10:pinA3_low(); pinA4_high();
    }
    /* ToDo: Hack for now: only display lowest layer. */
    if (cur_layer <= 0)
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

  pin_low(2);
  pin_mode_output(2);
  pin_low(3);
  pin_mode_output(3);
  pin_low(4);
  pin_mode_output(4);
  pin_low(5);
  pin_mode_output(5);
  pin_low(6);
  pin_mode_output(6);
  pin_low(7);
  pin_mode_output(7);
  pin_low(A0);
  pin_mode_output(A0);
  pin_low(A1);
  pin_mode_output(A1);
  pin_low(A2);
  pin_mode_output(A2);
  pin_low(A3);
  pin_mode_output(A3);
  pin_low(A4);
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

int
main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
  init();
  sei();

  pin13_mode_output();
  my_pin13_low();

  sleep_mode_idle();
  for (;;)
  {
/*
    sleep_enable();
    sleep_cpu();
    sleep_disable();
*/
  }
}
