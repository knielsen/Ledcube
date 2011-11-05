#include <util/delay.h>
#include <avr/sleep.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer1.h>


#define PIN_SCLK 8
#define PIN_SIN  9
#define PIN_XLAT 10
#define PIN_BLANK 11

/* 12-bit output values used for different pixel values. */
uint16_t pixel2out_high[16] =
{ 0, 0, 0, 0, 0,  0,  0,  0,  0,   0,   0, 445>>8,  775>>8,  1350>>8,  2352>>8,  4095>>8 };
uint16_t pixel2out_low[16] =
{ 0, 1, 3, 5, 9, 15, 27, 48, 84, 147, 255, 445&255, 775&255, 1350&255, 2352&255, 4095&255 };

#define NUM_LEDS 1337
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

static void
shift_out_frame(const uint8_t *data)
{
  uint8_t i, j;
  uint8_t bstate;
  uint8_t odd_even = 0;
  uint8_t pixel;

  bstate = portb_state & 0xf0;  /* XLAT, BLANK, XCLK all 0 */
  for (j = 0; j < 11; j++)
  {
    for (i = 0; i < LEDS_PER_LAYER; i++)
    {
      if (odd_even)
      {
        odd_even = 0;
        pixel = *data++ & 0xf;
      }
      else
      {
        odd_even = 1;
        pixel = *data >> 4;
      }
      shift_out_12bit(bstate, pixel2out_high[pixel], pixel2out_low[pixel]);
    }
    pin_high(PIN_BLANK);
    pin_high(PIN_XLAT);
    pin_low(PIN_XLAT);
    if (j == 0)
      pin_low(PIN_BLANK);
    else
      bstate |= 0x08;
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

//  serial_baud_9600();
//  serial_baud_115200();
//  serial_baud_230400();
//  serial_baud_250k();
  serial_baud_500k();
  serial_mode_8n1();
  serial_transmitter_enable();
  serial_receiver_enable();
  serial_interrupt_rx_enable();
}

int
main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
  uint8_t old_frame = 0xff, cur = 0;

  init();
  sei();

  pin13_mode_output();
  my_pin13_low();

  for (;;)
  {
    cli();
    cur = show_frame;
    if (cur != old_frame)
    {
      serial_write(frames[cur][1]);
      old_frame = cur;
    }
    sei();

    shift_out_frame(&frames[cur][4]);
  }
}
