/*
  Configuration for the LED cube.
*/

#ifndef LEDCUBE_H
#define LEDCUBE_H

#include <avr/pgmspace.h>
#include <stdlib.h>

/*
  Output pins for the layers. Can use any available GPIO.
  Layer 0 is the bottom layer.
  The code always uses 11 layers. All layers not in use can be set to any
  free GPIO.
*/
#define PIN_LAYER0 2
#define PIN_LAYER1 3
#define PIN_LAYER2 4
#define PIN_LAYER3 5
#define PIN_LAYER4 7
#define PIN_LAYER5 A5
#define PIN_LAYER6 A4
#define PIN_LAYER7 A3
#define PIN_LAYER8 A2
#define PIN_LAYER9 A1
#define PIN_LAYER10 A0

/* VPRG, XLAT, and BLANK can be on any available GPIO pin. */
#define PIN_VPRG 9
#define PIN_XLAT 10
#define PIN_BLANK 8

/*
  GSCLK must be on pin 6, as it uses the PWM of timer 0.
  To change, modifications of the code in ledcube.c will be necessary.
*/
#define PIN_GSCLK 6

/*
  SIN, SOUT, and SCLK must use pin 11-13, which is where hardware SPI is.

  Note that SIN is input to the TLC5940, so it corresponds to the output MOSI
  on the Atmega328. Similarly, SOUT is data coming out of TLC5940, so it
  corresponds to input MISO.

  Note that in addition to these, pin 10 must be configured as an output, or
  SPI will not function correctly (this is normally not a problem, as every
  signal we use except SOUT is an output).
*/
#define PIN_SIN  11
#define PIN_SOUT 12
#define PIN_SCLK 13


/* An array of these is passed in to define the animations to show. */
struct ledcube_anim {
  void (*anim_function)(uint8_t frame, uint16_t counter, uint16_t data);
  uint16_t data;
  uint16_t duration;
};


#ifdef __cplusplus
extern "C" {
#endif

  /*
    This function runs the cube code.
    It should be called from a main() function (don't use setup() and loop()).

    dc_value is the dot correction value, 63 is maximum current.
    num_tlcs is the number of TLC5940 chips attached.

    led_map gives the mapping of TLC outputs to LED columns.
    It must contain 128 numbers declared to be in flash like this:

        static const uint16_t led_map[] PROGMEM = { 0x8000, 100, ... };

    The entries correspond to the outputs of the TLC5940 ICs in reverse order.
    So the first number is the last output of the last TLC5940. The last number
    (led_map[127] is the first output on the first TLC5940.

    The numbering for the LED columns is like this (viewed from above)

                               For 11x11x11:
                                10  21  32  43  54  65  76  87  98 109 120
                                 9  20  31  42  53  64  75  86  97 108 119
                                 8  19  30  41  52  63  74  85  96 107 118
                                 7  18  29  40  51  62  73  84  95 106 117
                                 6  17  28  39  50  61  72  83  94 105 116
       For 5x5x5x:               5  16  27  38  49  60  71  82  93 104 115
       100  99  98  97  96       4  15  26  37  48  59  70  81  92 103 114
       105 104 103 102 101       3  14  25  36  47  58  69  80  91 102 113
       110 109 108 107 106       2  13  24  35  46  57  68  79  80 101 112
       115 114 113 112 111       1  12  23  34  45  56  67  78  89 100 111
       120 119 118 117 116       0  11  22  33  44  55  66  77  88  99 110
             FRONT                                  FRONT

    Putting one of these numbers into the led_map assigns the corresponding
    TLC5940 output to that LED column. Putting 0x8000 turns that output off
    permanently.

    So for example, if the first output of the first TLC5940 is unused, and
    the second is used for the front rightmost column, then the array should
    end with { ..., 116, 0x8000 } for 5x5x5, and { ..., 110, 0x8000 } for
    11x11x11.

    anim_table is a pointer to a NULL-terminated array of struct
    ledcube_anim defining which animations to show. Passing NULL gives a
    default animation.
  */

  void run_cube(const uint16_t * ledmap,
                uint8_t dc_value,
                uint8_t num_tlcs,
                const struct ledcube_anim *anim_table);

  /* Pixel plotting (for 5x5x5 respectively 11x11x11 cube). */
  void pixel5(uint8_t f, uint8_t x, uint8_t y, uint8_t z, uint8_t val);
  void pixel11(uint8_t f, uint8_t x, uint8_t y, uint8_t z, uint8_t val);

  /* Used to clear the framebuffer at the start of animation functions. */
  void fast_clear(uint8_t frame, uint8_t val);
  /*
    Used to make previous frames fade out, creating a nice warm afterglow
    effect.
  */
  void ef_afterglow(uint8_t frame, uint8_t subtract);

  /* Drawing planes on the 5x5x5 cube. */
  void draw_plane5(uint8_t f, float x0, float y0, float z0,
                   float nx, float ny, float nz, uint8_t col);

  /* Various animation functions. */
  void anim_solid(uint8_t f, uint16_t counter, uint16_t val);
  void anim_scan_plane(uint8_t f, uint16_t counter, uint16_t data);
  void anim_scan_plane_5(uint8_t f, uint16_t counter, uint16_t data);
  void anim_cornercube_5(uint8_t f, uint16_t counter, uint16_t speed);
  void anim_cosine_plane(uint8_t f, uint16_t counter, uint16_t data);
  void anim_stripes5(uint8_t frame, uint16_t counter, uint16_t speed);
  void anim_test_float(uint8_t f, uint16_t counter, uint16_t data);
  void anim_cosine_plane5(uint8_t f, uint16_t counter, uint16_t data);
  void anim_wobbly_plane5(uint8_t f, uint16_t counter, uint16_t speed);
  void anim_rotate_plane5(uint8_t f, uint16_t counter, uint16_t speed);

#ifdef __cplusplus
}
#endif

#endif  /* LEDCUBE_H */
