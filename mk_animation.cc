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
ef_clear(frame_xyz fb, uint8_t col= 0)
{
  memset(fb, col, sizeof(frame_xyz));
}

static void
ef_afterglow(frame_xyz fb, unsigned subtract)
{
  for (int x= 0; x < SIDE; x++)
    for (int y= 0; y < SIDE; y++)
      for (int z= 0; z < SIDE; z++)
        fb[x][y][z]= fb[x][y][z] > subtract ? fb[x][y][z] - subtract : 0;
}

static void
ef_scroll_y_up(frame_xyz fb)
{
  for (int x= 0; x < SIDE; ++x)
    for (int y= SIDE-2; y >= 0; --y)
      for (int z= 0; z < SIDE; ++z)
        fb[x][y+1][z]= fb[x][y][z];
}

static void
ef_scroll_y_down(frame_xyz fb)
{
  for (int x= 0; x < SIDE; ++x)
    for (int y= 1; y < SIDE; ++y)
      for (int z= 0; z < SIDE; ++z)
        fb[x][y-1][z]= fb[x][y][z];
}

static void
bubble5_a(frame_xyz F, int frame, void **data)
{
  ef_clear(F);
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

  ef_clear(F);
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
        cd->base_z= 4;
        cd->dir_z= -1;
      }
      else
      {
        cd->base_z= 0;
        cd->dir_z= 1;
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


static void
generic_scrolltext_5(frame_xyz F, int frame, char text[], size_t len)
{
  ef_clear(F);

  /* In the 5x5x5, there are 16 positions available. */
  int pos= (frame/4) % (len + 16) - 15;

  for (int i= 0; i < 16; ++i, ++pos)
  {
    if (pos < 0 || (size_t)pos >= len)
      continue;
    int x, y;
    if (i < 5)
    {
      x= 0;
      y= 4 - i;
    }
    else if (i < 9)
    {
      x= i - 4;
      y= 0;
    }
    else if (i < 13)
    {
      x= 4;
      y = i - 8;
    }
    else
    {
      x= 16 - i;
      y= 4;
    }
    uint8_t col;
    if (i < 3)
      col= 3 + i*4;
    else if (i > 12)
      col= 3 + (15-i)*4;
    else
      col= 15;
    for (int z= 0; z < 5; z++)
    {
      if (text[(4-z)*len+pos] != ' ')
        F[x][y][z]= col;
    }
  }
}

static char scrolltext_labitat[] =
"#    #  ##  ### ###  #  ###"
"#   # # # #  #   #  # #  # "
"#   ### ##   #   #  ###  # "
"#   # # # #  #   #  # #  # "
"### # # ##  ###  #  # #  # "
  ;

static char scrolltext_SiGNOUT[] =
"### #  ### #   #  ##  #  # ###"
"#     #    ##  # #  # #  #  # "
"### # # ## # # # #  # #  #  # "
"  # # #  # #  ## #  # #  #  # "
"### #  ### #   #  ##   ##   # "
  ;

static void
scrolltext_labitat_5(frame_xyz F, int frame, void **data)
{
  generic_scrolltext_5(F, frame, scrolltext_labitat,
                       (sizeof(scrolltext_labitat)-1)/(5*sizeof(char)));
}

static void
scrolltext_SiGNOUT_5(frame_xyz F, int frame, void **data)
{
  generic_scrolltext_5(F, frame, scrolltext_SiGNOUT,
                       (sizeof(scrolltext_SiGNOUT)-1)/(5*sizeof(char)));
}

static const char *
font5[256];

static void
init_font5()
{
  font5['A']=
    " X "
    "X X"
    "XXX"
    "X X"
    "X X";
  font5['B']=
    "XX "
    "X X"
    "XX "
    "X X"
    "XX ";
  font5['C']=
    " XX"
    "X  "
    "X  "
    "X  "
    " XX";
  font5['D']=
    "XX "
    "X X"
    "X X"
    "X X"
    "XX ";
  font5['E']=
    "XXX"
    "X  "
    "XX "
    "X  "
    "XXX";
  font5['F']=
    "XXX"
    "X  "
    "XX "
    "X  "
    "X  ";
  font5['G']=
    " XXX"
    "X   "
    "X XX"
    "X  X"
    " XXX";
  font5['H']=
    "X X"
    "X X"
    "XXX"
    "X X"
    "X X";
  font5['I']=
    "XXX"
    " X "
    " X "
    " X "
    "XXX";
  font5['J']=
    "XXX"
    "  X"
    "  X"
    "X X"
    " X ";
  font5['K']=
    "X X"
    "X X"
    "XX "
    "X X"
    "X X";
  font5['L']=
    "X  "
    "X  "
    "X  "
    "X  "
    "XXX";
  font5['M']=
    "X   X"
    "XX XX"
    "X X X"
    "X   X"
    "X   X";
  font5['N']=
    "X   X"
    "XX  X"
    "X X X"
    "X  XX"
    "X   X";
  font5['O']=
    " XX "
    "X  X"
    "X  X"
    "X  X"
    " XX ";
  font5['P']=
    "XX "
    "X X"
    "XX "
    "X  "
    "X  ";
  font5['Q']=
    " XX "
    "X  X"
    "X  X"
    "X XX"
    " XXX";
  font5['R']=
    "XXX "
    "X  X"
    "XXX "
    "X X "
    "X  X";
  font5['S']=
    " XX"
    "X  "
    " X "
    "  X"
    "XX ";
  font5['T']=
    "XXX"
    " X "
    " X "
    " X "
    " X ";
  font5['U']=
    "X  X"
    "X  X"
    "X  X"
    "X  X"
    " XX ";
  font5['V']=
    "X X"
    "X X"
    "X X"
    "X X"
    " X ";
  font5['W']=
    "X   X"
    "X   X"
    "X   X"
    "X X X"
    " X X ";
  font5['X']=
    "X X"
    "X X"
    " X "
    "X X"
    "X X";
  font5['Y']=
    "X X"
    "X X"
    " X "
    " X "
    " X ";
  font5['Z']=
    "XXX"
    "  X"
    " X "
    "X  "
    "XXX";
  font5[(unsigned char)'Æ']=
    " XXXX"
    "X X  "
    "XXXX "
    "X X  "
    "X XXX";
  font5[(unsigned char)'Ø']=
    "XXXX "
    "XX  X"
    "X X X"
    "X  XX"
    " XXXX";
  font5[(unsigned char)'Å']=
    " X "
    " X "
    "X X"
    "XXX"
    "X X";
  font5['0']=
    " X "
    "X X"
    "X X"
    "X X"
    " X ";
  font5['1']=
    "X"
    "X"
    "X"
    "X"
    "X";
  font5['2']=
    " XX "
    "X  X"
    "  X "
    " X  "
    "XXXX";
  font5['3']=
    " XX "
    "X  X"
    "  X "
    "X  X"
    " XX ";
  font5['4']=
    "X   "
    "X X "
    "XXXX"
    "  X "
    "  X ";
  font5['5']=
    "XXX"
    "X  "
    "XX "
    "  X"
    "XX ";
  font5['6']=
    "XXX"
    "X  "
    "XXX"
    "X X"
    "XXX";
  font5['7']=
    "XXX"
    "  X"
    "  X"
    " X "
    " X ";
  font5['8']=
    " XX "
    "X  X"
    " XX "
    "X  X"
    " XX ";
  font5['9']=
    "XXX"
    "X X"
    "XXX"
    "  X"
    "XXX";
}

static void
an_flytext5(frame_xyz F, int frame, void **data)
{
  static const int inter_letter_spacing= 6;
  const char *text= (const char *)*data;
  if ((frame % 2) == 0)
    ef_afterglow(F, 4);

  frame/= 2;
  for (int y= 0; y < 5 ; ++y)
  {
    if (((y - frame) % inter_letter_spacing) != 0)
      continue;
    int idx= (frame + (4-y))/inter_letter_spacing;
    int ch= text[idx % strlen(text)];

    const char *glyph= font5[ch];
    if (!glyph)
      continue;
    int glyph_size= strlen(glyph);
    int glyph_width= glyph_size/5;
    for (int z= 4; z >= 0; --z)
    {
      for (int i= 0; i < glyph_width; ++i)
      {
        if (*glyph++ != ' ')
          F[i+(5-glyph_width)/2][y][z]= 15;
      }
    }
  }
}

static void
an_icicles_5(frame_xyz F, int frame, void **data)
{
  static const int maxN= 10;
  static const int stage2= 6;
  struct st_icicle {
    int count;
    int start_frame[maxN];
    int end_frame[maxN];
    int x[maxN];
    int y[maxN];
    double height[maxN];
  };

  if (!*data)
    *data= static_cast<void *>(new struct st_icicle);
  if (!frame)
    memset(*data, 0, sizeof(struct st_icicle));
  struct st_icicle *cd= static_cast<struct st_icicle *>(*data);

  if ((cd->count < maxN) && (cd->count == 0 || rand() % 45 == 0))
  {
    /* Add a new one. */
    cd->start_frame[cd->count]= frame;
    cd->end_frame[cd->count]= frame + 50 + rand()/(RAND_MAX/50);
    cd->x[cd->count]= rand() % 5;
    cd->y[cd->count]= rand() % 5;
    cd->height[cd->count]= 1.5 + (double)rand() / ((double)RAND_MAX/1.6);
    ++cd->count;
  }

  ef_clear(F);

  for (int i= 0; i < cd->count; )
  {
    if (frame <= cd->end_frame[i] - stage2)
    {
      /* Slowly growing. */
      double progress= ((double)frame - (double)cd->start_frame[i]) /
        ((double)cd->end_frame[i] - stage2 - (double)cd->start_frame[i]);
      double height= progress * cd->height[i];
      int colour= 3 + 5.5 * progress;
      int z;
      for (z= 0; z < height; ++z)
        F[cd->x[i]][cd->y[i]][z]= colour;
      F[cd->x[i]][cd->y[i]][z]= colour * (height - (z-1));
      ++i;
    }
    else if (frame <= cd->end_frame[i])
    {
      double progress= 1.0 - (double)(cd->end_frame[i] - frame)/(double)stage2;
      int colour= progress > 0.2 ? 15 : 11 + 18 * progress;
      int z= progress * 4;
      while (z <= 4)
      {
        F[cd->x[i]][cd->y[i]][z]= colour;
        ++z;
      }
      ++i;
    }
    else
    {
      /* Delete this one, it's done. */
      --cd->count;
      cd->start_frame[i]= cd->start_frame[cd->count];
      cd->end_frame[i]= cd->end_frame[cd->count];
      cd->x[i]= cd->x[cd->count];
      cd->y[i]= cd->y[cd->count];
      cd->height[i]= cd->height[cd->count];
    }
  }
}

/* Highligt x-axis (y=z=0) */
static void
testimg_x_axis(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 7);
  for (uint8_t x= 0; x < SIDE; ++x)
    F[x][0][0]= 15;
  F[0][1][0]= 12;
  F[0][0][1]= 12;
}

static void
testimg_y_axis(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 7);
  for (uint8_t y= 0; y < SIDE; ++y)
    F[0][y][0]= 15;
  F[1][0][0]= 12;
  F[0][0][1]= 12;
}

static void
testimg_z_axis(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 7);
  for (uint8_t z= 0; z < SIDE; ++z)
    F[0][0][z]= 15;
  F[1][0][0]= 12;
  F[0][1][0]= 12;
}

static void
testimg_solid(frame_xyz F, int frame, void **data)
{
  //ef_clear(F, (frame/30) % 16);
  ef_clear(F, 13);
}

static void
testimg_rect5(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 0);
  for (int i= 0; i < 5; i++)
  {
    F[i][0][0]= 15;
    F[i][4][0]= 15;
    F[0][i][0]= 15;
    F[4][i][0]= 15;
  }
}

/* Cycle each LED in the bottom layer. */
static void
testimg_walk_bottom_5(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 0);
  int v= frame / 30;
  F[v % 5][(v/5) % 5][0] = 15;
}

/* Show the different grey scales for easy comparison. */
static void
testimg_show_greyscales_5(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 0);
  for (int z=0; z<5; ++z)
  {
    F[0][0][z]= F[1][0][z]= F[0][1][z]= F[1][1][z]= z+1;
    F[0][3][z]= F[1][3][z]= F[0][4][z]= F[1][4][z]= z+4;
    F[3][0][z]= F[4][0][z]= F[3][1][z]= F[4][1][z]= z+7;
    F[3][3][z]= F[4][3][z]= F[3][4][z]= F[4][4][z]= z+11;
  }
}

static void
testimg_show_greyscales_bottom_5(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 0);
  for (int x=0; x<5; ++x)
    for (int y=0; y<5; ++y)
      F[x][y][0]= frame/30 + (x*3+y) % 16;
}

/* ****************************************************************** */
static void (*out_function)(frame_xyz);
static int frame_repeat= 1;
static int loop= 0;

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
frame_out_ledpro5(frame_xyz fb)
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
  {
    for (int i= 120; i >= 0; --i)
    {
      unsigned val;
      if (i >= 25)
        val= 0;
      else
      {
        int x= i % 5;
        int y= i / 5;
        val= fb[x][y][z];
      }
      if (odd_even)
      {
        putchar(partial | (val & 0xf));
        checksum^= partial | (val & 0xf);
        odd_even= 0;
      }
      else
      {
        partial= (val & 0xf) << 4;
        odd_even= 1;
      }
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
play_animation(struct anim_piece *animation)
{
  frame_xyz framebuf;

loop:
  struct anim_piece *anim= animation;
  ef_clear(framebuf);
  while (anim->frame_func)
  {
    for (int frame= 0; frame < anim->num_frames; frame++)
    {
      (*anim->frame_func)(framebuf, frame, &anim->data);
      for (int i= 0; i < frame_repeat; ++i)
        (*out_function)(framebuf);
    }
    anim++;
  }
  if (loop)
    goto loop;
}


static struct anim_piece animation1[] = {
  // { testimg_x_axis, 100000, 0},
  // { testimg_y_axis, 100000, 0},
  // { testimg_z_axis, 100000, 0},
  // { testimg_solid, 100000, 0},
  // { testimg_rect5, 100000, 0},
  // { testimg_walk_bottom_5, 100000, 0},
  // { testimg_show_greyscales_5, 100000, 0},
  // { testimg_show_greyscales_bottom_5, 100000, 0},
  { an_icicles_5, 600, 0 },
  { scrolltext_labitat_5, 400, 0 },
  { fade_out, 16, 0 },
  { scrolltext_SiGNOUT_5, 400, 0 },
  { fade_out, 16, 0 },
  { bubble5_a, 600, 0 },
  { fade_out, 16, 0 },
  { cornercube_5, 15*20, 0 },
  { fade_out, 16, 0 },
  { an_flytext5, 500, (void *)" LABITAT  " },
  { 0, 0, 0}
};


int
main(int argc, char *argv[])
{
  init_font5();

  out_function= frame_out_5;
  while (--argc > 0)
  {
    ++argv;
    if (0 == strcmp(argv[0], "--ledpro"))
      out_function= frame_out_ledpro;
    else if (0 == strcmp(argv[0], "--ledpro5"))
      out_function= frame_out_ledpro5;
    else if (0 == strcmp(argv[0], "--loop"))
      loop= 1;
    else if (0 == strncmp(argv[0], "--repeat=", 9))
    {
      frame_repeat= atoi(&argv[0][9]);
      if (frame_repeat < 1)
        frame_repeat= 1;
    }
    else
    {
      fprintf(stderr, "Unknown option '%s'\n", argv[0]);
      exit(1);
    }
  }
  play_animation(animation1);
  return 0;
}
