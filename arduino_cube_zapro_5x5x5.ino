#include <oniudra.h>
#include <ledcube.h>

static const uint16_t led_map_zapro_5x5x5[] PROGMEM = {
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,

  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,

  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,

  105, 100, 99, 104, 98, 103, 97, 102,
  96, 101, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
  106, 111, 116, 117, 112, 107, 118, 113,
  108, 119, 114, 109, 120, 115, 110, 0x8000,
};

static const uint8_t frame_L_A[] PROGMEM = {
  B00000, B10000, B00000, B00100, B00000,
  B00000, B10000, B00000, B01010, B00000,
  B00000, B10000, B00000, B11111, B00000,
  B00000, B10000, B00000, B10001, B00000,
  B00000, B11111, B00000, B10001, B00000
};

static const uint8_t frame_PER[] PROGMEM = {
  B11100, B00000, B11111, B00000, B11110,
  B10010, B00000, B10000, B00000, B10001,
  B11100, B00000, B11100, B00000, B11110,
  B10000, B00000, B10000, B00000, B10100,
  B10000, B00000, B11111, B00000, B10010
};


static void
anim_show_binary_array(uint8_t f, uint16_t counter, uint16_t data)
{
  const uint8_t *ptr = (const uint8_t *)data;
  uint8_t x, y, z;

  fast_clear(f, 0);
  for (z = 0; z < 5; ++z)
  {
    for (y = 0; y < 5; ++y)
    {
      uint8_t row = pgm_read_byte(&ptr[20-z*5+y]);
      for (x = 0; x < 5; ++x)
      {
        if (row & (1 << (4-x)))
          pixel5(f, x, y, z, 15);
      }
    }
  }
}


static void
anim_my_test(uint8_t f, uint16_t counter, uint16_t data)
{
  byte x, y, z, col;
  if (counter % data == 0)
    fast_clear(f, 0);

  x = rand() / (RAND_MAX/5+1);
  y = rand() / (RAND_MAX/5+1);
  z = rand() / (RAND_MAX/5+1);
  col = 5 + rand() / (RAND_MAX/10+1);
  pixel5(f, x, y, z, col);
}

static const struct ledcube_anim animation[] PROGMEM = {
  { anim_show_binary_array, (uint16_t)frame_PER, 2400 },
  { anim_show_binary_array, (uint16_t)frame_L_A, 2400 },
  { anim_my_test, 500, 1500 },
  { anim_scan_plane_5, 15, 1200 },
  { anim_cosine_plane5, 22, 800 },
  { anim_cornercube_5, 52, 2400 },
  { anim_rotate_plane5, 5, 2000 },
  { anim_wobbly_plane5, 5, 2000 },
  { anim_test_float, 15, 500 },
  { anim_stripes5, 4, 2400 },
  { NULL, 0, 0 },
};

int main(int argc, char *argv[]) {
  run_cube(led_map_zapro_5x5x5, 12, 2, animation);
}

