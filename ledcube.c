#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/pgmspace.h>
#include <stdlib.h>

#include <arduino/pins.h>
#include <arduino/serial.h>
#include <arduino/timer0.h>
#include <arduino/timer1.h>
#include <arduino/sleep.h>
#include <arduino/spi.h>

/* This is the current through each LED, relative to MAX, 0..63. */
#define DC_VALUE 2
/*#define DEBUG_OUTPUT_STATUS_INFO_REGISTER*/

#define PIN_GSCLK 6
#define PIN_VPRG 9
#define PIN_XLAT 10
#define PIN_SIN  11
#define PIN_BLANK 8
#define PIN_SCLK 13
#define PIN_SOUT 12

#define NUM_LEDS 1331
#define NUM_LAYERS 11
#define LEDS_PER_LAYER 121
#define BITS_PER_LED 4
#define DATA_SIZE ((NUM_LEDS*BITS_PER_LED+7)/8)
#define FRAME_SIZE (DATA_SIZE + 6)
#define NUM_FRAMES 2

static uint8_t frames[NUM_FRAMES][FRAME_SIZE];

/* Mapping: for each LED, which nibble to take the grayscale value from. */
/* For the fast SPI output, the size should be divisible by two. */
/* Note: pin 3 (output 4 of 16) of IC 6 is re-mapped to pin 15 (output 16) */
/* of IC 8, due to shorted pad - this is nibble 103. */
static const uint16_t led_map[] PROGMEM = {
  15, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 52,
  63, 74, 75, 64, 53, 76, 65, 54,
  40, 41, 42, 43, 30, 29, 31, 32,
  21, 20, 19, 10, 18, 9, 8, 7,
  61, 49, 37, 50, 38, 26, 51, 27,
  39, 28, 17, 16, 0x8000, 6, 5, 4,
  36, 25, 14, 3, 24, 35, 13, 2,
  1, 12, 23, 0, 34, 11, 22, 33,
  60, 70, 69, 59, 58, 68, 48, 47,
  57, 46, 45, 56, 67, 44, 55, 66,
  80, 79, 78, 77, 90, 91, 89, 88,
  99, 100, 101, 110, 102, 111, 112, 113,
  73, 72, 62, 83, 71, 94, 82, 81,
  93, 92, 103, 104, 105, 114, 115, 116,
  84, 95, 106, 117, 118, 85, 96, 107,
  119, 108, 97, 120, 86, 109, 98, 87,
};

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
/* Running checksum. */
static uint8_t checksum = 0;

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
    "lds  r30, checksum\n\t"
    "eor  r30, r0\n\t"
    "sts   checksum, r30\n\t"

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
  checksum ^= c;
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
    if (checksum != 0xff || frames[cur][0] != 0x0 ||
        frames[cur][FRAME_SIZE-1] != 0xff)
    {
      /* Frame error. Skip this frame and send error to peer. */
#ifndef DEBUG_OUTPUT_STATUS_INFO_REGISTER
      serial_write(frames[cur][1] | 0x80);
#endif
      checksum = 0;
      return;
    }
    checksum = 0;
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


/* 12-bit output values used for different pixel values. */
static uint16_t pixel2out[16] =
#ifdef DAYMODE
{ 0, 23, 89, 192, 332, 508, 719, 964, 1242, 1554, 1898, 2275, 2684, 3125, 3597, 4095 };
#else
//{ 0, 1, 3, 5, 9, 15, 27, 48, 84, 147, 255, 445, 775, 1350, 2352, 4095 };
{ 0, 5, 27, 73, 150, 263, 414, 609, 851, 1142, 1486, 1886, 2344, 2863, 3446, 4095 };
#endif

static void
shift_out_frame_spi(uint8_t *frame_start, uint16_t start)
{
  register uint8_t *frame_start_r asm("r20") = frame_start;
  register uint8_t leds asm("r25") = sizeof(led_map)/sizeof(led_map[0])/2;
  register uint16_t start_r asm("r18") = start;

  /* Init SPI */
  pin_mode_output(11);  /* MOSI */
  pin_mode_output(13);  /* SCK */
  /*
    Interrupt disable, SPI enable, MSB first, master mode, SCK idle low,
    sample on leading edge, d2 clock.
  */
  SPCR = _BV(SPE) | _BV(MSTR);
  /* d2 clock. */
  SPSR = _BV(SPI2X);

  /*
    Shift out all the grayscale values for one layer.
    Use hardware SPI at max speed (2 cycles/bit) so that we can in parallel on
    the CPU do the word to fetch and prepare the bits for the next one.

    Carefully count the cycles and spread out the SPI transmit OUT
    instructions, so that we know that SPI will be ready and save a couple
    instructions to wait for SPI ready.

    In each loop iteration we fetch two nibbles of grayscale values and convert
    them into 3 bytes (2*12 bits) to shift out. So the number of LEDS to shift
    out must be even (it is safe to shift out one more dummy LED.

    The led_map array, in FLASH memory, holds the nibble offset into the frame
    for each LED to shift out. An offset >= 0x8000 shifts out zero.

    The pixel2out array, in DATA memory, holds 16 uint16_t values for the
    PWM value to shift out to the TLC59xx for each possible grayscale value.
  */
  asm volatile
  (
  "ldi  r30, lo8(led_map)\n\t"
  "ldi  r31, hi8(led_map)\n\t"
  "ldi  r22, lo8(pixel2out)\n\t"
  "ldi  r23, hi8(pixel2out)\n\t"
  "ldi  r16, 0\n"                 /* dummy initial shift-out of zeros. */

"1:\n\t"
  "lpm  r26, Z+\n\t"       /* 3 */
  "lpm  r27, Z+\n\t"       /* 3  idx = led_map[led_num++] */
  "add  r26, r18\n\t"      /* 1 */
  "adc  r27, r19\n\t"      /* 1  X = idx + start */
  "brmi 4f\n\t"            /* 1  (2 for taken) */

  "mov  r0, r26\n\t"       /* 1  save copy of X low bit, so we can test later */
  "lsr  r27\n\t"           /* 1 */
  "ror  r26\n\t"           /* 1 */
  "add  r26, r20\n\t"      /* 1 */
  "adc  r27, r21\n\t"      /* 1  X = &frame[(idx+start)/2] */
  "ld   r26, X\n\t"        /* 1 */
  "sbrs r0,0\n\t"          /* 1  (2 if skip, but that's counting swap as 0) */
  "swap r26\n\t"           /* 1 */
  "out  0x2E, r16\n\t"     /* 1 */
  "andi r26, 0xf\n"        /* 1 */

"2:\n\t"                  /*    Now r26 = pixel value */
  "clr  r27\n\t"           /* 1 */
  "add  r26, r26\n\t"      /* 1 */
  "add  r26, r22\n\t"      /* 1 */
  "adc  r27, r23\n\t"      /* 1  X = &pixel2out[pixel] */
  "ld   r24, X+\n\t"       /* 2  r24 = v  (low 8 bits to shift out) */
  "ld   r0, X\n\t"         /* 1  r0 = u  (high 4 bits to shift out) */
  "swap r0\n\t"            /* 1 */
  "swap r24\n\t"           /* 1 */
  "ldi  r17, 0xf\n\t"      /* 1 */
  "and  r17, r24\n\t"      /* 1 */
  "or   r17, r0\n\t"       /* 1 */

/* Now the second one ... */

  "lpm  r26, Z+\n\t"       /* 3 */
  "lpm  r27, Z+\n\t"       /* 3 */
  "add  r26, r18\n\t"      /* 1 */
  "out  0x2E,r17\n\t"      /* 1 */
  "adc  r27, r19\n\t"      /* 1 */
  "brmi 7f\n\t"            /* 1 */
  "mov  r0, r26\n\t"       /* 1 */
  "lsr  r27\n\t"           /* 1 */
  "ror  r26\n\t"           /* 1 */
  "add  r26, r20\n\t"      /* 1 */
  "adc  r27, r21\n\t"      /* 1 */
  "ld   r26, X\n\t"        /* 1 */
  "sbrs r0,0\n\t"          /* 1 */
  "swap r26\n\t"           /* 1 */
  "andi r26, 0xf\n"        /* 1 */

"3:\n\t"
  "clr  r27\n\t"           /* 1 */
  "add  r26, r26\n\t"      /* 1 */
  "add  r26, r22\n\t"      /* 1 */
  "adc  r27, r23\n\t"      /* 1 */
  "ld   r16, X+\n\t"       /* 2 */
  "ld   r0, X\n\t"         /* 1 */
  "andi r24, 0xf0\n\t"     /* 1 */
  "or   r0, r24\n\t"       /* 1 */
  "out  0x2E, r0\n\t"      /* 1 */
  "dec  r25\n\t"           /* 1 */
  "brne 1b\n\t"            /* 2 */

  "rjmp 10f\n"             /* 1 (really 2, but subtract 1 for not taken brne) */

"4:\n\t"                   /* 1  (extra for taken branch) */
  "clr  r26\n\t"           /* 1 */
  /* Need to burn 5 cycles here ... */
  "rjmp 5f\n"              /* 2 */
"5:\n\t"
  "rjmp 6f\n"              /* 2 */
"6:\n\t"
  "nop\n\t"                /* 1 */
  "out  0x2E, r16\n\t"     /* 1  # Note: this OUT is one cycle early */
  "rjmp 2b\n"              /* 2 */

"7:\n\t"                   /* 1  (extra for taken branch) */
  "clr  r26\n\t"           /* 1 */
  /* Need to burn 5 cycles here ... */
  "rjmp 8f\n"              /* 2 */
"8:\n\t"
  "rjmp 9f\n"              /* 2 */
"9:\n\t"
  "nop\n\t"                /* 1 */
  "rjmp 3b\n"              /* 2 */

"10:\n\t"
  /* Now we just need to wait 17 cycles and then shift out the last 8 bits. */
  "ldi r17, 5\n"           /* 1 */
"11:\n\t"
  "dec r17\n\t"            /* 1 */
  "brne 11b\n\t"           /* 2 / 1 */
  "nop\n\t"                /* 1 */
  "out  0x2E, r16\n\t"     /* 1 */
  /* And wait for final to be shifted out. */
  "ldi r17, 7\n"           /* 1 */
"12:\n\t"
  "dec r17\n\t"            /* 1 */
  "brne 12b\n\t"           /* 2 / 1 */
  : "+r" (leds)
  : "r" (start_r), "r" (frame_start_r), "i" (&led_map[0]), "i" (&pixel2out[0])
  /* We do not clobber memory, but we do read it. I couldn't easily figure out
     how to specify exactly what we read (the frames[][] array, and it's not
     time critical, so a generic "memory" clobber will serve.
  */
  : "r0", "r16", "r17", "r22", "r23", "r24", "r26", "r27", "r30", "r31", "memory"
  );

  /* De-init SPI */
  spi_disable();
}


static uint8_t old_frame= 0xff;
static uint8_t cur_layer= NUM_LAYERS;
static volatile uint8_t frame_refresh_counter= 0;
timer1_interrupt_a()
{
  static uint8_t *frame_start;
  static uint16_t start;

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
    case 0: pinA0_high(); pin2_low(); break;
    case 1: pin2_high(); pin3_low(); break;
    case 2: pin3_high(); pin4_low(); break;
    case 3: pin4_high(); pin5_low(); break;
    case 4: pin5_high(); pin7_low(); break;
    case 5: pin7_high(); pinA5_low(); break;
    case 6: pinA5_high(); pinA4_low(); break;
    case 7: pinA4_high(); pinA3_low(); break;
    case 8: pinA3_high(); pinA2_low(); break;
    case 9: pinA2_high(); pinA1_low(); break;
    case 10:pinA1_high(); pinA0_low(); break;
    }

    pin_low(PIN_BLANK);
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
#ifndef DEBUG_OUTPUT_STATUS_INFO_REGISTER
      serial_write(frames[cur_frame][1]);
#endif
      old_frame= cur_frame;
    }
    frame_start = &frames[cur_frame][4];
    start = 0;
  }

  /* Now shift out one layer. */
  shift_out_frame_spi(frame_start, start);
  start += 121;
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
  pin_high(A5);
  pin_mode_output(A5);

  /* NOTE: pin 6 is used for GSCLK. */
  pin_high(PIN_GSCLK);
  pin_mode_output(PIN_GSCLK);

  /* VPRG is on pin 9. High initially to set in DC mode. */
  pin_high(PIN_VPRG);
  pin_mode_output(PIN_VPRG);

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
  timer1_compare_a_set(2*4096);   /* 16MHz / (2*4096) -> 1953Hz. */
  //timer1_compare_a_set(17778);   /* 16MHz / 177778 -> 900Hz. */
  //timer1_compare_a_set(35955);   /* 16MHz / 35955 -> 445Hz. */
  timer1_interrupt_a_enable();

  /* Use compare register A on timer0 to generate an 8MHz PWM clock on pin 6. */
  TCCR0B = 0x01;  /* WGM0=2 (CTC) CS0=1 (no prescaling) */
  TCCR0A = 0x42;  /* COM0A=1 (toggle) COM0B=0 (off) WGM0=2 (CTC) */
  OCR0A = 0;      /* Compare value 0 -> max freq. (8 MHz). */
  TCNT0 = 0;
}

/*
  Load the DC (dot correction) values into the TLC5940's.
  This sets the current through each LED.
  A value of 0 turns off, a value of 63 means max as determined by the
  IREF resistor.
*/
static void
init_dc(void)
{
  uint8_t byte;
  uint8_t i, j, k;
  uint8_t mask;
  uint8_t *p, *q;

  /* Init SPI */
  pin_mode_output(PIN_SIN);
  pin_mode_output(PIN_SCLK);
  pin_mode_input(PIN_SOUT);

  /*
    Interrupt disable, SPI enable, MSB first, master mode, SCK idle low,
    sample on leading edge, d2 clock.
  */
  SPCR = _BV(SPE) | _BV(MSTR);
  /* d2 clock. */
  SPSR = _BV(SPI2X);

  /* Set DC mode. */
  pin_high(PIN_VPRG);

  mask = 0x80;
  byte = 0;
  for (i= 0; i < 128; ++i)
  {
    uint8_t v= DC_VALUE;
    int8_t bit;
    for (bit = 5; bit >= 0; --bit)
    {
      if (v & (1 << bit))
        byte |= mask;
      mask >>= 1;
      if (mask == 0)
      {
        /* Shift out the byte with the SPI hardware. */
        spi_write(byte);
        while (!spi_interrupt_flag())
          ;
        mask = 0x80;
        byte = 0;
      }
    }
  }

  /* Now latch the new DC data. */
  pin_high(PIN_XLAT);
  pin_low(PIN_XLAT);

  /*
    Now shift in initial all-zero grayscale data, and read out status at the
    same time.
  */

  /* Set grayscale mode. */
  pin_low(PIN_VPRG);

  /*
    The TLC5940 actually needs setup and sample both on the trailing edge. But
    AVR SPI can only do setup and sample on opposite edges.
    So we just sample on trailing and setup on leading - and setup an initial
    zero bit before we start - that works since we shift out all zeros anyway.
  */

  pin_low(PIN_SIN);
  SPCR = _BV(SPE) | _BV(MSTR) | _BV(CPHA);
  SPSR = _BV(SPI2X);

  pin_high(PIN_XLAT); pin_low(PIN_XLAT);
  /*
    Datasheet explains that a small delay is needed before status info data
    becomes available.
  */
  _delay_us(1.51 + 0.125);

  p = &frames[0][0];
  for (i = 0; i < 192*8/8; ++i)
  {
    spi_write(0);
    while (!spi_interrupt_flag())
      ;
    *p++ = spi_read();
  }

  /* De-init SPI */
  spi_disable();

#ifdef DEBUG_OUTPUT_STATUS_INFO_REGISTER
  /* Now output the stuff, for dbug. */
  while (!serial_writeable()); serial_write('?');
  for (q= &frames[0][0]; q < p; ++q)
  {
    uint8_t u, v;
    v = *q;
    u = v >> 4;
    while (!serial_writeable()); serial_write(u + (u >= 10 ? 'A'-10 : '0'));
    u = v & 0xf;
    while (!serial_writeable()); serial_write(u + (u >= 10 ? 'A'-10 : '0'));
  }
  while (!serial_writeable()); serial_write('!');
#endif

  /*
    Check that the DC register reads identical to what we tried to write.
    If not, refuse to continue.
    The idea is to avoid running with random data in the DC due to software
    or hardware errors, potentially frying the LEDs.
  */
  p= &frames[0][0];
  for (i = 0; i < 8; ++i)
  {
    /* The first 3 bytes are LOD and TEF error flags. */
    p+= 3;
    /* Then the 16 6-bit DC register values. */
    mask= 0x80;
    for (j = 0; j < 16; ++j)
    {
      byte= 0;
      for (k = 0; k < 6; ++k)
      {
        byte <<= 1;
        if (*p & mask)
          byte|= 1;
        mask >>= 1;
        if (!mask)
        {
          mask= 0x80;
          ++p;
        }
      }
      if (byte != DC_VALUE)
        goto err;
    }
    /* The last 9 bytes are reserved. */
    p+= 9;
  }
err:
  while (byte != DC_VALUE)
  {
    /* Flash the status LED forever to show our unhappiness */
    pin_mode_output(13);
    pin_high(13);
    _delay_ms(300);
    pin_low(13);
    _delay_ms(100);
    pin_high(13);
    _delay_ms(100);
    pin_low(13);
    _delay_ms(100);
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
    *p++= v;
}


static void anim_solid(uint8_t f, uint8_t val);
static void anim_scan_plane(uint8_t f);
static void anim_scan_plane_5(uint8_t f);
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
  uint8_t i;

  init();
  /* Wait a bit to allow TLC5940's and electronics power to settle. */
  _delay_ms(1000);
  init_dc();
  /* Clear the buffers. */
  for (i = 0; i < NUM_FRAMES; ++i)
    fast_clear(i, 0);
  sei();

  sleep_mode_idle();
  for (;;)
  {
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
      anim_solid(generate_frame, 15);
      //anim_solid(generate_frame, 0);
      //anim_scan_plane(generate_frame);
      //anim_scan_plane_5(generate_frame);
      //cornercube_5(generate_frame);
    }
    ++generate_counter;
  }
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

static void
pixel11(uint8_t f, uint8_t x, uint8_t y, uint8_t z, uint8_t val)
{
  uint8_t *p;
  uint8_t v;
  int idx= LEDS_PER_LAYER * z;
  idx += x*NUM_LAYERS;
  idx += y;

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
  static uint16_t count = 0;
  fast_clear(f, val);
  //fast_clear(f, (++count/10)%16);
}

static void
anim_scan_plane(uint8_t f)
{
  static unsigned int count = 0;
  int i, j;
  fast_clear(f, 0);
  for (i = 0; i < 11; ++i)
  {
    for (j = 0; j < 11; ++j)
    {
      pixel11(f, i, (count/32)%11, j, 15);
    }
  }
  ++count;
}

static void
anim_scan_plane_5(uint8_t f)
{
  static unsigned int count = 0;
  int i, j;
  fast_clear(f, 0);
  for (i = 0; i < 5; ++i)
  {
    for (j = 0; j < 5; ++j)
    {
      pixel5(f, i, (count/32)%5, j, 15);
    }
  }
  ++count;
}
