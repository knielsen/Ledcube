#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#define SIDE 11
#define NBITS 4
#define GLVLS (1<<NBITS)

typedef uint8_t frame_xyz[SIDE][SIDE][SIDE];

struct anim_piece {
  void (*frame_func)(frame_xyz, int, void**);
  int num_frames;
  void *data;
};


static void
clear(frame_xyz fb)
{
  memset(fb, 0, sizeof(frame_xyz));
}


static void
bubble5_a(frame_xyz F, int frame, void **data)
{
  clear(F);
  for (int x= 0; x < 5; x++)
  {
    for (int y= 0; y < 5; y++)
    {
      for (int z= 0; z < 5; z++)
      {
        double d= sqrt((x-2)*(x-2)+(y-2)*(y-2)+(z-2)*(z-2));
        double h= (double)frame / 4 + d*4;
        int col= (int)h % GLVLS;
        F[x][y][z]= col;
      }
    }
  }
}


static void
fade_out(frame_xyz F, int frame, void **data)
{
  if (!*data)
    *data= malloc(sizeof(frame_xyz));
  frame_xyz *orig_F= static_cast<frame_xyz *>(*data);
  if (frame == 0)
  {
    // Save starting frame.
    memcpy(orig_F, F, sizeof(frame_xyz));
  }

  double fade_factor= (double)(15-frame) / 15;
  if (fade_factor < 0)
    fade_factor= 0;
  for (int x= 0; x < SIDE; ++x)
    for (int y= 0; y < SIDE; ++y)
      for (int z= 0; z < SIDE; ++z)
        F[x][y][z]= (int)(0.5 + (double)((*orig_F)[x][y][z]) * fade_factor);
}


struct cornercube_data {
  int base_x, base_y, base_z;
  int dir_x, dir_y, dir_z;
  int col, target_col;
  int corner;
};

static void
cornercube_5(frame_xyz F, int frame, void **data)
{
  if (!*data)
    *data= static_cast<void *>(new struct cornercube_data);
  struct cornercube_data *cd= static_cast<struct cornercube_data *>(*data);

  static const int base_count= 15;

  clear(F);
  if (frame == 0)
  {
    // Initialise first corner to expand from
    cd->col= 0;
    cd->target_col= 8 + rand() % 8;
    cd->base_x= 0;
    cd->base_y= 0;
    cd->base_z= 0;
    cd->dir_x= 1;
    cd->dir_y= 1;
    cd->dir_z= 1;
    cd->corner= 0;
  }

  if ((frame / (base_count+1))%2 == 0)
  {
    // Cube expanding from corner.
    if ((frame % (base_count+1)) == 0)
    {
      cd->col= cd->target_col;
      cd->target_col= 8 + rand() % 8;
    }
    double expand_factor= (double)(frame % (base_count+1)) / base_count;
    int col= (int)((double)cd->col +
                   expand_factor * ((double)cd->target_col - (double)cd->col) + 0.5);
    int side_len= (int)(expand_factor * 4 + 0.5);
    for (int i= 0; i <= side_len; i++)
    {
      int end_x= cd->base_x+cd->dir_x*side_len;
      int end_y= cd->base_y+cd->dir_y*side_len;
      int end_z= cd->base_z+cd->dir_z*side_len;
      F[cd->base_x + i*cd->dir_x][cd->base_y][cd->base_z]= col;
      F[cd->base_x + i*cd->dir_x][end_y][cd->base_z]= col;
      F[cd->base_x + i*cd->dir_x][cd->base_y][end_z]= col;
      F[cd->base_x + i*cd->dir_x][end_y][end_z]= col;
      F[cd->base_x][cd->base_y + i*cd->dir_y][cd->base_z]= col;
      F[end_x][cd->base_y + i*cd->dir_y][cd->base_z]= col;
      F[cd->base_x][cd->base_y + i*cd->dir_y][end_z]= col;
      F[end_x][cd->base_y + i*cd->dir_y][end_z]= col;
      F[cd->base_x][cd->base_y][cd->base_z + i*cd->dir_z]= col;
      F[end_x][cd->base_y][cd->base_z + i*cd->dir_z]= col;
      F[cd->base_x][end_y][cd->base_z + i*cd->dir_z]= col;
      F[end_x][end_y][cd->base_z + i*cd->dir_z]= col;
    }
  }
  else
  {
    // Cube retreating to other corner.
    if ((frame % (base_count+1)) == 0)
    {
      // At first frame, select new base point.
      int corner;
      do
        corner= rand() % 8;
      while (corner == cd->corner);
      cd->corner= corner;
      if (corner & 1)
      {
        cd->base_x= 4;
        cd->dir_x= -1;
      }
      else
      {
        cd->base_x= 0;
        cd->dir_x= 1;
      }
      if (corner & 2)
      {
        cd->base_y= 4;
        cd->dir_y= -1;
      }
      else
      {
        cd->base_y= 0;
        cd->dir_y= 1;
      }
      if (corner & 4)
      {
        cd->base_x= 4;
        cd->dir_x= -1;
      }
      else
      {
        cd->base_x= 0;
        cd->dir_x= 1;
      }
      // And a new target colour.
      cd->col= cd->target_col;
      cd->target_col= 8 + rand() % 8;
    }
    double expand_factor= (double)(base_count - (frame % (base_count + 1))) / base_count;
    int col= (int)((double)cd->col +
                   expand_factor * ((double)cd->target_col - (double)cd->col) + 0.5);
    int side_len= (int)(expand_factor * 4 + 0.5);
    for (int i= 0; i <= side_len; i++)
    {
      int end_x= cd->base_x+cd->dir_x*side_len;
      int end_y= cd->base_y+cd->dir_y*side_len;
      int end_z= cd->base_z+cd->dir_z*side_len;
      F[cd->base_x + i*cd->dir_x][cd->base_y][cd->base_z]= col;
      F[cd->base_x + i*cd->dir_x][end_y][cd->base_z]= col;
      F[cd->base_x + i*cd->dir_x][cd->base_y][end_z]= col;
      F[cd->base_x + i*cd->dir_x][end_y][end_z]= col;
      F[cd->base_x][cd->base_y + i*cd->dir_y][cd->base_z]= col;
      F[end_x][cd->base_y + i*cd->dir_y][cd->base_z]= col;
      F[cd->base_x][cd->base_y + i*cd->dir_y][end_z]= col;
      F[end_x][cd->base_y + i*cd->dir_y][end_z]= col;
      F[cd->base_x][cd->base_y][cd->base_z + i*cd->dir_z]= col;
      F[end_x][cd->base_y][cd->base_z + i*cd->dir_z]= col;
      F[cd->base_x][end_y][cd->base_z + i*cd->dir_z]= col;
      F[end_x][end_y][cd->base_z + i*cd->dir_z]= col;
    }
  }
}


static void (*out_function)(frame_xyz);

static void
frame_out_5(frame_xyz fb)
{
  static const char col2ascii[]= " .,-~_+=o*m#&@XM";
  printf("\n");
  for (int y= 4; y >= 0; --y)
  {
    for (int z= 0; z < 5; z++)
    {
      printf("  ");
      for (int x= 0; x < 5; x++)
      {
        printf("%c", col2ascii[fb[x][y][z]]);
      }
    }
    printf("\n");
  }
}


/*
  Frame format:
  <0> <frame counter 0-63> <len_low> <len_high> <data> <checksum> <0xff>
*/
static void
frame_out_ledpro(frame_xyz fb)
{
  static uint64_t frame_count= 0;
  uint8_t checksum= 0;

  putchar(0);
  checksum^= 0;
  putchar(frame_count % 64);
  checksum^= frame_count % 64;
  ++frame_count;
  uint16_t len= (SIDE*SIDE*SIDE*NBITS+7)/8;
  putchar(len & 0xff);
  checksum^= len & 0xff;
  putchar(len >> 8);
  checksum^= len >> 8;

  int odd_even= 0;
  uint8_t partial;
  for (int z= 0; z < SIDE ; ++z)
    for (int y= 0; y < SIDE; ++y)
      for (int x= 0; x < SIDE; ++x)
      {
        if (odd_even)
        {
          putchar(partial | (fb[x][y][z] & 0xf));
          checksum^= partial | (fb[x][y][z] & 0xf);
          odd_even= 0;
        }
        else
        {
          partial= (fb[x][y][z] & 0xf) << 4;
          odd_even= 1;
        }
      }
  if (odd_even)
  {
    putchar(partial);
    checksum^= partial;
  }

  putchar(checksum);
  putchar(0xff);
}


static void
play_animation(struct anim_piece *anim)
{
  frame_xyz framebuf;

  clear(framebuf);
  while (anim->frame_func)
  {
    for (int frame= 0; frame < anim->num_frames; frame++)
    {
      (*anim->frame_func)(framebuf, frame, &anim->data);
      (*out_function)(framebuf);
    }
    anim++;
  }
}


static struct anim_piece animation1[] = {
  { bubble5_a, 600, 0 },
  { fade_out, 16, 0 },
  { cornercube_5, 15*20, 0 },
  { fade_out, 16, 0 },
  { 0, 0, 0}
};


int
main(int argc, char *argv[])
{
  out_function= frame_out_5;
  if (argc == 2 && 0 == strcmp(argv[1], "--ledpro"))
    out_function= frame_out_ledpro;
  play_animation(animation1);
  return 0;
}
