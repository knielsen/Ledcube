#include <util/delay.h>
#include <avr/sleep.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer1.h>


#define PIN_SCLK 8
#define PIN_SIN  9
#define PIN_XLAT 10
#define PIN_BLANK 11

/* 12-bit value used for an "on" diode. */
#define VAL_ON 4095


#define NUM_LEDS 1337
#define BITS_PER_LED 1
#define FRAME_SIZE ((NUM_LEDS*BITS_PER_LED+7)/8)
#define NUM_FRAMES 4

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
  0x7d <byte1> <byte2> ... <byteN> <checksum>
  bytes 0x7d/0x7e are sent as 0x7e 0x00 / 0x7e 0x01.
*/
serial_interrupt_rx()
{
  uint8_t c;
  static uint8_t current_frame = 0;
  static uint16_t current_idx = 0;
  static uint8_t checksum = 0, escape_mode = 0;

   if (current_frame % 2)
     my_pin13_low();
   else
    my_pin13_high();

  c = serial_read();
  if (c == 0x7d)
  {
    /* Next frame. */
    current_frame = (current_frame + 1) % NUM_FRAMES;
    current_idx = 0;
    escape_mode = 0;
    checksum = 0;
  }
  else if (c == 0x7e)
  {
    escape_mode = 1;
  }
  else
  {
    if (escape_mode)
    {
      c = 0x7d + c;
      escape_mode = 0;
    }
    if (current_idx < FRAME_SIZE)
    {
      frames[current_frame][current_idx] = c;
      ++current_idx;
      checksum ^= c;
    }
    else
    {
      if (checksum != c)
        my_pin13_high();   /* flag checksum error */
    }
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

static void
shift_out_24(const uint8_t *data)
{
  uint8_t i;
  uint16_t v;
  uint8_t bstate;
  /*
    Bits are shifted out in reverse, from highest bit of last output to lowest
    bit of first output.
  */
  i = 24;
  bstate = portb_state & 0xf0;  /* XLAT, BLANK, XCLK all 0 */
  do
  {
    --i;
    v = ( data[i/8] & (1 << (i % 8)) ? VAL_ON : 0 );
    shift_out_12bit(bstate, v >> 8, v & 0xff);
  } while (i);

  pin_high(PIN_BLANK);
  pin_high(PIN_XLAT);
  pin_low(PIN_XLAT);
  pin_low(PIN_BLANK);
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

  serial_baud_115200();
  serial_mode_8n1();
  serial_transmitter_enable();
  serial_receiver_enable();
  serial_interrupt_rx_enable();
}

int
main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
  uint8_t frame1[3], frame2[3];
  uint8_t i;
  uint16_t old_time, time;
    const uint8_t *frame;

  init();
  sei();

  timer1_mode_normal();
  timer1_clock_d1024();  /* 16e6/1024 = 15.625KHz. */

  pin13_mode_output();
  my_pin13_low();

  for (i = 0; i < 3; i++)
  {
    frame1[i] = 0x55;
    frame2[i] = 0xaa;
  }

  old_time = timer1_count();
  for (;;)
  {
    /*
      Wait for next frame.
      One frame is eg. 1 / (60fps * 11planes) = 1.515msec -> 24 timer ticks.
    */
    while ((time = timer1_count()) - old_time  < 24)
      ;
    if (time & 0x4000)
    {
      frame = frame1;
    }
    else
    {
      frame = frame2;
    }

    shift_out_24(frame);
  }
}
