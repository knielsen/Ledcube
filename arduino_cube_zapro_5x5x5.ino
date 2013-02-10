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
  run_cube(led_map_zapro_5x5x5, 32, 2, animation);
}
