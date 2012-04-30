#define __USE_XOPEN 1
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#define SIDE 11
#define NBITS 4
#define GLVLS (1<<NBITS)

/* Random integer 0 <= x < N. */
static int
irand(int n)
{
  return rand() / (RAND_MAX/n+1);
}

/* Random double 0 <= x <= N. */
static double
drand(double n)
{
  return (double)rand() / ((double)RAND_MAX/n);
}

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
        double h= (double)frame / 2.4 + d*4;
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


/*
  Draw a plane given starting point R0 and normal vector N.
  For now, requires that the plane is "mostly horizontal", meaning that the
  largest component of the normal is in the z direction. Later we will
  generalise to arbitrary normal, by selecting the two driving directions
  to be the smaller two components of the normal.
*/
static void
draw_plane(double x0, double y0, double z0, double nx, double ny, double nz,
           frame_xyz F, int sidelen)
{
  if (nx > nz || ny > nz)
    return; /* ToDo */

  /*
    We span the plane with the two vectors A=(1,0,-nx/nz) and B=(0,1,-ny/nz).

    These are normal to N and linearly independent, so they _do_span the plane.
    And they are convenient for scan conversion, as they have unit component
    in the x respectively y direction.

    We shift the starting point to
        Q0 = R0 - x0*A - y0*B = (0, 0, z0-x0*nx/nz-y0*ny/nz)
    Then we can generate the points of the plane as simply
        R = Q0 + u*A + v*B = (u, v, z0+(u-x0)*nx/nz+(v-y0)*ny/nz)
    This makes it easy to scan-convert with one voxel per column.
  */

  for (int i = 0; i < sidelen; ++i)
  {
    for (int j = 0; j < sidelen; ++j)
    {
      int k = round(z0 + (i-x0)*nx/nz + (j-y0)*ny/nz);
      if (k >= 0 && k < sidelen)
        F[i][j][k] = 15;
    }
  }
}


/* Draw a line from start to end. Handles clipping. */
static void
draw_line(frame_xyz F, double x0, double y0, double z0,
          double x1, double y1, double z1, int val)
{
  /*
    Find the major axis (x, y, or z) along which the line direction has the
    largest component. We will draw the line so that a plane normal to this
    axis intersects the drawn line in exactly one voxel.
  */
  double dx = x1 - x0;
  double dy = y1 - y0;
  double dz = z1 - z0;

  double d, start, end, a, b, d1, d2;
  int dir;
  if (fabs(dx) >= fabs(dy) && fabs(dx) >= fabs(dz))
  {
    /*
      The equation for the line is
          (x,y,z) = (x0,y0,z0) + t(dx,dy,dz)
      Let u = t*dx to rewrite this as
          (x,y,z) = (x0,y0,z0) + u*(1,dy/dx,dz/dx)
      Use this to draw along the x-axis in unit steps.
      (Similar for the y and z directions).
    */
    d = dx; d1 = dy; d2 = dz;
    start = x0; end = x1;
    a = y0; b = z0;
    dir = 0;
  }
  else if (fabs(dy) >= fabs(dx) && fabs(dy) >= fabs(dz))
  {
    d = dy; d1 = dx; d2 = dz;
    start = y0; end = y1;
    a = x0; b = z0;
    dir = 1;
  }
  else
  {
    d = dz; d1 = dx; d2 = dy;
    start = z0; end = z1;
    a = x0;
    b = y0;
    dir = 2;
  }
  if (d < 0)
  {
    /* Swap so we can draw "left-to-right" to simplify bounds conditions. */
    double tmp;
    tmp = start; start = end; end = tmp;
    a += d1; b += d2;
    d = -d; d1 = -d1; d2 = -d2;
  }
  int i = round(start);
  if (i < 0)
    i = 0;
  int last_i = round(end);
  if (last_i >= SIDE)
    last_i = SIDE-1;
  /* Adjust starting point for rounding and possibly clipping of i. */
  a = a + (i - start) * (d1/d);
  b = b + (i - start) * (d2/d);
  while (i <= last_i)
  {
    int j = round(a);
    int k = round(b);
    if (j >= 0 && j < SIDE && k >= 0 && k < SIDE)
    {
      switch (dir)
      {
      case 0: F[i][j][k] = val; break;
      case 1: F[j][i][k] = val; break;
      case 2: F[j][k][i] = val; break;
      }
    }
    a += d1/d;
    b += d2/d;
    ++i;
  }
}


/* Cross product. */
static void
ut_cross_prod(double x1, double y1, double z1,
              double x2, double y2, double z2,
              double *rx, double *ry, double *rz)
{
  *rx = y1*z2-y2*z1;
  *ry = z1*x2-z2*x1;
  *rz = x1*y2-x2*y1;
}

/* Rotate plane coordinates a given angle V. */
static void ut_rotate(double *x, double *y, double v)
{
  double x2 = cos(v) * *x - sin(v) * *y;
  *y = sin(v) * *x + cos(v) * *y;
  *x = x2;
}

/* Rotate a vector around an axis. */
static void
ut_rotate_axis(double ax, double ay, double az,
               double *px, double *py, double *pz, double v)
{
  /*
    First we construct two unit normals to the given axis.
    We do this by starting with a vector parallel to the x/y/z axis that
    has the smallest component along the given axis, and then doing
    cross-producs.
    Then we project the vector to rotate onto the normal plane spanned by
    the two normals.
    We then rotate the vector in that plane, and finally contruct the
    rotated vector back from the two normals and original vector.
  */
  double a = sqrt(ax*ax+ay*ay+az*az);
  ax /= a;
  ay /= a;
  az /= a;
  double n1x, n1y, n1z, n2x, n2y, n2z, n1, n2;
  if (fabs(ax) <= fabs(ay) && fabs(ax) <= fabs(az))
  {
    n1x = 1; n1y = 0; n1z = 0;
  }
  else if (fabs(ay) <= fabs(ax) && fabs(ay) <= fabs(az))
  {
    n1x = 0; n1y = 1; n1z = 0;
  }
  else if (fabs(az) <= fabs(ax) && fabs(az) <= fabs(ay))
  {
    n1x = 0; n1y = 0; n1z = 1;
  }
  ut_cross_prod(ax, ay, az, n1x, n1y, n1z, &n2x, &n2y, &n2z);
  n2 = sqrt(n2x*n2x+n2y*n2y+n2z*n2z);
  n2x /= n2;
  n2y /= n2;
  n2z /= n2;
  ut_cross_prod(ax, ay, az, n2x, n2y, n2z, &n1x, &n1y, &n1z);
  /*
    n1 will be a unit vector already, as it is the cross product of two
    perpendicular unit vectors. So this normalisation is really redundant ...
  */
  n1 = sqrt(n1x*n1x+n1y*n1y+n1z*n1z);
  n1x /= n1;
  n1y /= n1;
  n1z /= n1;
  double x = *px; double y = *py; double z = *pz;
  double c1 = x*n1x + y*n1y + z*n1z;
  double c2 = x*n2x + y*n2y + z*n2z;
  double c3 = x*ax + y*ay + z*az;
  ut_rotate(&c1, &c2, v);
  *px = c1*n1x + c2*n2x + c3*ax;
  *py = c1*n1y + c2*n2y + c3*ay;
  *pz = c1*n1z + c2*n2z + c3*az;
}


/* Animation with misc. wireframe objects. */
struct st_wireframe {
  struct { double x1,y1,z1,x2,y2,z2; } lines[50];
  int num;
};

static void
ut_wireframe_add_line(struct st_wireframe *c,
                      double x1, double y1, double z1,
                      double x2, double y2, double z2)
{
  int i = c->num;
  if (i < sizeof(c->lines)/sizeof(c->lines[0]))
  {
    c->lines[i].x1 = x1;
    c->lines[i].y1 = y1;
    c->lines[i].z1 = z1;
    c->lines[i].x2 = x2;
    c->lines[i].y2 = y2;
    c->lines[i].z2 = z2;
    c->num = i+1;
  }
}

static void
an_wireframe(frame_xyz F, int frame, void **data)
{
  static const double omega1 = 1.45/15.38;
  static const double omega2 = 1.45/11.71;
  static const double omega3 = 1.45/28.44;
  static const int stage_dur = 175;
  static const int fade_dur = 30;

  if (frame == 0)
    *data = malloc(sizeof(struct st_wireframe));
  struct st_wireframe *c= static_cast<struct st_wireframe *>(*data);


  if (frame == 0)
  {
    c->num = 0;
  }

  if (frame % stage_dur == 0)
  {
    c->num = 0;
    switch ((frame/stage_dur) % 9)
    {
    case 0:
      /* Cube. */
      ut_wireframe_add_line(c, -3, -3, -3, 3, -3, -3);
      ut_wireframe_add_line(c, -3, 3, -3, 3, 3, -3);
      ut_wireframe_add_line(c, -3, -3, 3, 3, -3, 3);
      ut_wireframe_add_line(c, -3, 3, 3, 3, 3, 3);
      ut_wireframe_add_line(c, -3, -3, -3, -3, 3, -3);
      ut_wireframe_add_line(c, 3, -3, -3, 3, 3, -3);
      ut_wireframe_add_line(c, -3, -3, 3, -3, 3, 3);
      ut_wireframe_add_line(c, 3, -3, 3, 3, 3, 3);
      ut_wireframe_add_line(c, -3, -3, -3, -3, -3, 3);
      ut_wireframe_add_line(c, 3, -3, -3, 3, -3, 3);
      ut_wireframe_add_line(c, -3, 3, -3, -3, 3, 3);
      ut_wireframe_add_line(c, 3, 3, -3, 3, 3, 3);
      break;
    case 1:
    {
      /* Tetraeder */
      static const double M = 4.5;
      double a = M*sqrt(3)/2.0;
      ut_wireframe_add_line(c, -a, -0.5*M, -0.5*M, a, -0.5*M, -0.5*M);
      ut_wireframe_add_line(c, -a, -0.5*M, -0.5*M, 0, M, -0.5*M);
      ut_wireframe_add_line(c, 0, M, -0.5*M, a, -0.5*M, -0.5*M);
      ut_wireframe_add_line(c, -a, -0.5*M, -0.5*M, 0, 0, M);
      ut_wireframe_add_line(c, a, -0.5*M, -0.5*M, 0, 0, M);
      ut_wireframe_add_line(c, 0, M, -0.5*M, 0, 0, M);
      break;
    }
    case 2:
    {
      /* "L" */
      double A = -2.5;
      double B = 4;
      ut_wireframe_add_line(c, -A, 0, B, -A, 0, -B);
      ut_wireframe_add_line(c, -A, 0, -B, A, 0, -B);
      break;
    }
    case 3:
    case 7:
    {
      /* "A" */
      double A = 3.4;
      double B = 4;
      double C = -1;
      double D = A*(B-C)/(2*B);
      ut_wireframe_add_line(c, -A, 0, -B, 0, 0, B);
      ut_wireframe_add_line(c, A, 0, -B, 0, 0, B);
      ut_wireframe_add_line(c, -D, 0, C, D, 0, C);
      break;
    }
    case 4:
    {
      /* "B" */
      double A = 3;
      double B = 4;
      double C = 2;
      ut_wireframe_add_line(c, -A, 0, B, 0, 0, B);
      ut_wireframe_add_line(c, 0, 0, B, A, 0, C);
      ut_wireframe_add_line(c, A, 0, C, 0, 0, 0);
      ut_wireframe_add_line(c, 0, 0, 0, A, 0, -C);
      ut_wireframe_add_line(c, A, 0, -C, 0, 0, -B);
      ut_wireframe_add_line(c, 0, 0, -B, -A, 0, -B);
      ut_wireframe_add_line(c, -A, 0, -B, -A, 0, B);
      break;
    }
    case 5:
    {
      /* I */
      double A = -2;
      double B = 4;
      ut_wireframe_add_line(c, -A, 0, B, A, 0, B);
      ut_wireframe_add_line(c, -A, 0, -B, A, 0, -B);
      ut_wireframe_add_line(c, 0, 0, -B, 0, 0, B);
      break;
    }
    case 6:
    case 8:
    {
      /* T */
      double A = -2.5;
      double B = 4;
      ut_wireframe_add_line(c, -A, 0, B, A, 0, B);
      ut_wireframe_add_line(c, 0, 0, -B, 0, 0, B);
      break;
    }
    default:
      fprintf(stderr, "an_wireframe: error: fix modulus in switch() "
              "to match number of objects.\n");
      abort();
    }
  }

  ef_clear(F);
  for (int i = 0 ; i < c->num; ++i)
  {
    double x1 = c->lines[i].x1;
    double y1 = c->lines[i].y1;
    double z1 = c->lines[i].z1;
    double x2 = c->lines[i].x2;
    double y2 = c->lines[i].y2;
    double z2 = c->lines[i].z2;
    ut_rotate(&x1, &z1, (double)frame*omega3);
    ut_rotate(&x2, &z2, (double)frame*omega3);
    ut_rotate(&x1, &y1, (double)frame*omega1);
    ut_rotate(&x2, &y2, (double)frame*omega1);
    ut_rotate(&y1, &z1, (double)frame*omega2);
    ut_rotate(&y2, &z2, (double)frame*omega2);

    /* Handle fade-in / fade-out. */
    int col;
    double factor = -1;
    int d = frame % stage_dur;
    if (d < fade_dur)
      factor = (double)d / (double)fade_dur;
    else if (stage_dur - d < fade_dur)
      factor = (double)(stage_dur - d) / (double)fade_dur;
    if (factor >= 0)
    {
      col = round(15.49*factor);
      x1 *= factor;
      y1 *= factor;
      z1 *= factor;
      x2 *= factor;
      y2 *= factor;
      z2 *= factor;
    }
    else
      col = 15;

    x1 += ((double)SIDE-1)/2;
    y1 += ((double)SIDE-1)/2;
    z1 += ((double)SIDE-1)/2;
    x2 += ((double)SIDE-1)/2;
    y2 += ((double)SIDE-1)/2;
    z2 += ((double)SIDE-1)/2;

    draw_line(F, x1, y1, z1, x2, y2, z2, col);
  }
}


struct st_fountain {
  int num;
  double count;
  struct { double x, y, z, vx, vy, vz, damp; int base; } p[200];
};

static void
an_fountain(frame_xyz F, int frame, void **data)
{
  static const int rate = 1.7;
  static const double spread = 0.19*M_PI/2.0;
  static const double min_damp = 0.12;
  static const double max_damp = 0.35;
  static const double min_height = 6.5;
  static const double max_height = 10.2;
  static const double radius = 0.8;
  static const double g = 0.057;
  static const double v_damp = 0.012;

  if (frame == 0)
    *data = malloc(sizeof(struct st_fountain));
  struct st_fountain *c= static_cast<struct st_fountain *>(*data);

  if (frame == 0)
  {
    c->num = 0;
  }

  c->count += drand(rate);
  while (c->count > 0 && c->num < sizeof(c->p)/sizeof(c->p[0]))
  {
    /* Add a new one. */
    /*
      We don't want to take a uniform distribution of the vertical
      angle - that would give too much bias to mostly vertical directions.
    */
    double v = spread*(1 - pow(drand(1), 1.5));
    double u = drand(2*M_PI);
    double h = min_height + drand(max_height - min_height);
    double V = sqrt(2*g*h);
    double r = drand(radius);

    int i = c->num++;
    c->p[i].vx = V*cos(u)*sin(v);
    c->p[i].vy = V*sin(u)*sin(v);
    c->p[i].vz = V*cos(v);
    c->p[i].x = r*cos(u) + ((double)SIDE-1)/2;
    c->p[i].y = r*sin(u) + ((double)SIDE-1)/2;
    c->p[i].z = -2;
    c->p[i].base = frame;
    c->p[i].damp = min_damp + drand(max_damp - min_damp);

    --c->count;
  }

  for (int i = 0; i < c->num; ++i)
  {
    c->p[i].x += c->p[i].vx;
    c->p[i].y += c->p[i].vy;
    c->p[i].z += c->p[i].vz;
    c->p[i].vz -= g;
    c->p[i].vx -= v_damp*c->p[i].vx;
    c->p[i].vy -= v_damp*c->p[i].vy;
    if (c->p[i].z < 0 && frame - c->p[i].base > 5)
      c->p[i].z = 0;
  }

  ef_clear(F);
  for (int i = 0; i < c->num; )
  {
    int x = round(c->p[i].x);
    int y = round(c->p[i].y);
    int z = round(c->p[i].z);
    double col = 15.0 - c->p[i].damp * (frame - c->p[i].base);
    if (col <= 0)
    {
      /* Delete it. */
      c->p[i] = c->p[--c->num];
      continue;
    }
    if (x >= 0 && x < SIDE && y >= 0 && y < SIDE && z >= 0 && z < SIDE)
      F[x][y][z] = round(col);
    ++i;
  }
}


struct st_fireworks {
  int num_phase1;
  int num_phase2;
  struct { double x[3],y[3],z[3],vx,vy,vz,s,col; int base_frame, delay;
           double gl_base, gl_period, gl_amp; } p1[10];
  struct { double x,y,z,vx,vy,vz,col; int base_frame, delay;
           double fade_factor; } p2[300];
};

static void
ut_fireworks_shiftem(struct st_fireworks *c, int i)
{
  for (int j = sizeof(c->p1[0].x)/sizeof(c->p1[0].x[0]) - 1; j > 0; --j)
  {
    c->p1[i].x[j] = c->p1[i].x[j-1];
    c->p1[i].y[j] = c->p1[i].y[j-1];
    c->p1[i].z[j] = c->p1[i].z[j-1];
  }
}

static void
an_fireworks(frame_xyz F, int frame, void **data)
{
  if (frame == 0)
    *data = malloc(sizeof(struct st_fireworks));
  struct st_fireworks *c= static_cast<struct st_fireworks *>(*data);

  static const int max_phase1 = sizeof(c->p1)/sizeof(c->p1[0]);
  static const int max_phase2 = sizeof(c->p2)/sizeof(c->p2[0]);
  static const double g = 0.045;
  static const int new_freq = 85;
  static const double min_height = 6;
  static const double max_height = 10;
  static const int min_start_delay = 32;
  static const int max_start_delay = 67;
  static const int min_end_delay = 50;
  static const int max_end_delay = 100;
  const double V = 0.5;
  static const double resist = 0.11;
  static const double min_fade_factor = 0.22;
  static const double max_fade_factor = 0.27;

  if (frame == 0)
  {
    c->num_phase1 = 0;
    c->num_phase2 = 0;
  }

  /* Start a new one occasionally. */
  if (c->num_phase1 == 0 || (c->num_phase1 < max_phase1 && irand(new_freq) == 0))
  {
    int i = c->num_phase1++;
    c->p1[i].x[0] = SIDE/2.0 - 2.0 + drand(4);
    c->p1[i].y[0] = SIDE/2.0 - 2.0 + drand(4);
    c->p1[i].z[0] = 0;
    for (int j = 0; j < sizeof(c->p1[0].x)/sizeof(c->p1[0].x[0]) - 1; ++j)
      ut_fireworks_shiftem(c, i);

    c->p1[i].vx = drand(0.4) - 0.2;
    c->p1[i].vy = drand(0.4) - 0.2;
    c->p1[i].s = min_height + drand(max_height - min_height);
    c->p1[i].vz = sqrt(2*g*c->p1[i].s);
    c->p1[i].col = 8;
    c->p1[i].base_frame = frame;
    c->p1[i].delay = min_start_delay + irand(max_start_delay - min_start_delay);
    c->p1[i].gl_base = frame;
    c->p1[i].gl_period = 0;
  }

  for (int i = 0; i < c->num_phase1; )
  {
    int d = frame - c->p1[i].base_frame;
    if (d < c->p1[i].delay)
    {
      /* Waiting for launch - make fuse glow effect. */
      int gl_delta = frame - c->p1[i].gl_base;
      if (gl_delta >= c->p1[i].gl_period)
      {
        c->p1[i].gl_base = frame;
        c->p1[i].gl_period = 8 + irand(6);
        c->p1[i].gl_amp = 0.7 + drand(0.3);
        gl_delta = 0;
      }
      double glow = c->p1[i].gl_amp*sin((double)gl_delta/c->p1[i].gl_period*M_PI);
      c->p1[i].col = round(7.0 + 5.0*glow);
      ++i;
    }
    else if (c->p1[i].z[0] > c->p1[i].s)
    {
      /* Kaboom! */
      /* Delete this one, and create a bunch of phase2 ones (if room). */
      int k = 10 + irand(20);
      while (k-- > 0)
      {
        if (c->num_phase2 >= max_phase2)
          break;            /* No more room */
        int j = c->num_phase2++;
        /*
          Sample a random direction uniformly.
          Uses the fact that cylinder projection of the sphere is area
          preserving, so sample uniformly the cylinder, and project onto
          the sphere.
        */
        double v = drand(2*M_PI);
        double u = drand(2.0) - 1.0;
        double r = sqrt(1.0 - u*u);
        double vx = V*r*cos(v);
        double vy = V*r*sin(v);
        double vz = V*u;
        c->p2[j].x = c->p1[i].x[0];
        c->p2[j].y = c->p1[i].y[0];
        c->p2[j].z = c->p1[i].z[0];
        c->p2[j].vx = c->p1[i].vx + vx;
        c->p2[j].vy = c->p1[i].vy + vy;
        c->p2[j].vz = c->p1[i].vz + vz;
        c->p2[j].col = 15;
        c->p2[j].base_frame = frame;
        c->p2[j].delay = min_end_delay + irand(max_end_delay - min_end_delay);
        c->p2[j].fade_factor =
          min_fade_factor + drand(max_fade_factor - min_fade_factor);
      }
      c->p1[i] = c->p1[--c->num_phase1];
    }
    else
    {
      ut_fireworks_shiftem(c, i);
      c->p1[i].col =12;
      c->p1[i].x[0] += c->p1[i].vx;
      c->p1[i].y[0] += c->p1[i].vy;
      c->p1[i].z[0] += c->p1[i].vz;
      c->p1[i].vz -= g;
      ++i;
    }
  }

  for (int i = 0; i < c->num_phase2;)
  {
    c->p2[i].x += c->p2[i].vx;
    c->p2[i].y += c->p2[i].vy;
    c->p2[i].z += c->p2[i].vz;

    c->p2[i].vx -= resist*c->p2[i].vx;
    c->p2[i].vy -= resist*c->p2[i].vy;
    c->p2[i].vz -= resist*c->p2[i].vz + g;

    double col = 15 - c->p2[i].fade_factor*(frame - c->p2[i].base_frame);
    c->p2[i].col = col < 0 ? 0 : round(col);

    if (c->p2[i].z <= 0)
    {
      c->p2[i].z = 0;
      if (c->p2[i].delay-- <= 0)
      {
        /* Delete it. */
        c->p2[i] = c->p2[--c->num_phase2];
      }
      else
        ++i;
    }
    else
      ++i;
  }

  ef_clear(F);
  /*
    Draw stage2 first, so we don't overwrite a new rocket with an old, dark
    ember.
  */
  for (int i = 0; i < c->num_phase2; ++i)
  {
    int x = round(c->p2[i].x);
    int y = round(c->p2[i].y);
    int z = round(c->p2[i].z);
    if (x >= 0 && x < SIDE && y >= 0 && y < SIDE && z >= 0 && z < SIDE)
      F[x][y][z] = round(c->p2[i].col);
  }
  for (int i = 0; i < c->num_phase1; ++i)
  {
    for (int j = 0; j < sizeof(c->p1[0].x)/sizeof(c->p1[0].x[0]); ++j)
    {
      int x = round(c->p1[i].x[j]);
      int y = round(c->p1[i].y[j]);
      int z = round(c->p1[i].z[j]);
      if (x >= 0 && x < SIDE && y >= 0 && y < SIDE && z >= 0 && z < SIDE)
        F[x][y][z] = round(c->p1[i].col);
    }
  }
}


static const char *
font9[256];

static void
init_font9()
{
  font9['A']=
    "    X    "
    "   XXX   "
    "   XXX   "
    "  XX XX  "
    "  XX XX  "
    " XXXXXXX "
    " XXXXXXX "
    "XX     XX"
    "XX     XX";
  font9['B']=
    "XXXXX "
    "XXXXXX"
    "XX  XX"
    "XX  XX"
    "XXXXX "
    "XX  XX"
    "XX  XX"
    "XXXXXX"
    "XXXXX ";
  font9['C']=
    " XXXXXXXX"
    "XXXXXXXXX"
    "XX       "
    "XX       "
    "XX       "
    "XX       "
    "XX       "
    "XXXXXXXXX"
    " XXXXXXXX";
  font9['D']=
    "XXXXXXXX "
    "XXXXXXXXX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXXX"
    "XXXXXXXX ";
  font9['E']=
    "XXXXXXXXX"
    "XXXXXXXXX"
    "XX       "
    "XX       "
    "XXXXX    "
    "XX       "
    "XX       "
    "XXXXXXXXX"
    "XXXXXXXXX";
  font9['F']=
    "XXXXXXXXX"
    "XXXXXXXXX"
    "XX       "
    "XX       "
    "XXXXX    "
    "XX       "
    "XX       "
    "XX       "
    "XX       ";
  font9['G']=
    " XXXXXXXX"
    "XXXXXXXXX"
    "XX       "
    "XX       "
    "XX    XXX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXXX"
    " XXXXXXX ";
  font9['H']=
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXXX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX";
  font9['I']=
    "XXXXXX"
    "XXXXXX"
    "  XX  "
    "  XX  "
    "  XX  "
    "  XX  "
    "  XX  "
    "XXXXXX"
    "XXXXXX";
  font9['J']=
    "       XX"
    "       XX"
    "       XX"
    "       XX"
    "       XX"
    "       XX"
    "XX     XX"
    "XXXXXXXXX"
    " XXXXXXX ";
  font9['K']=
    "XX    XX"
    "XX   XX "
    "XX  XX  "
    "XX XX   "
    "XXXX    "
    "XX XX   "
    "XX  XX  "
    "XX   XX "
    "XX    XX";
  font9['L']=
    "XX     "
    "XX     "
    "XX     "
    "XX     "
    "XX     "
    "XX     "
    "XX     "
    "XXXXXXX"
    "XXXXXXX";
  font9['M']=
    "XX     XX"
    "XXX   XXX"
    "XX X X XX"
    "XX XXX XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX";
  font9['N']=
    "XX     XX"
    "XXX    XX"
    "XXXX   XX"
    "XX XX  XX"
    "XX  XX XX"
    "XX   XXXX"
    "XX    XXX"
    "XX     XX"
    "XX     XX";
  font9['O']=
    " XXXXXXX "
    "XXXXXXXXX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXXX"
    " XXXXXXX ";
  font9['P']=
    "XXXXXXXX "
    "XXXXXXXXX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXX "
    "XX       "
    "XX       "
    "XX       "
    "XX       ";
  font9['Q']=
    " XXXXXXX "
    "XXXXXXXXX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX   X XX"
    "XX    XXX"
    "XXXXXXXXX"
    " XXXXXXXX";
  font9['R']=
    "XXXXXXXX "
    "XXXXXXXXX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXX "
    "XXXX     "
    "XX  XX   "
    "XX   XX  "
    "XX    XX ";
  font9['S']=
    " XXXXXXX "
    "XXXXXXXX "
    "XX       "
    "XX       "
    " XXXXXXX "
    "       XX"
    "       XX"
    " XXXXXXXX"
    " XXXXXXX ";
  font9['T']=
    "XXXXXXXX"
    "XXXXXXXX"
    "   XX   "
    "   XX   "
    "   XX   "
    "   XX   "
    "   XX   "
    "   XX   "
    "   XX   ";
  font9['U']=
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XXXXXXXXX"
    " XXXXXXX ";
  font9['V']=
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    " XX   XX "
    "  XX XX  "
    "   XXX   "
    "    X    ";
  font9['W']=
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX     XX"
    "XX  XX XX"
    " XXX XXX "
    "  X   X  ";
  font9['X']=
    "XX     XX"
    "XX     XX"
    " XX   XX "
    "  XX XX  "
    "   XXX   "
    "  XX XX  "
    " XX   XX "
    "XX     XX"
    "XX     XX";
  font9['Y']=
    "XX     XX"
    "XX     XX"
    " XX   XX "
    "  XX XX  "
    "   XXX   "
    "    X    "
    "    X    "
    "    X    "
    "    X    ";
  font9['Z']=
    "XXXXXXXXX"
    "XXXXXXXXX"
    "     XX  "
    "    XX   "
    "   XX    "
    "  XX     "
    " XX      "
    "XXXXXXXXX"
    "XXXXXXXXX";
  font9[' ']=
    "      "
    "      "
    "      "
    "      "
    "      "
    "      "
    "      "
    "      "
    "      ";
  font9['-']=
    "     "
    "     "
    "     "
    "     "
    "XXXXX"
    "     "
    "     "
    "     "
    "     ";
  font9['!']=
    " XX "
    " XX "
    " XX "
    " XX "
    " XX "
    " XX "
    "    "
    " XX "
    " XX ";
  font9['.']=
    "    "
    "    "
    "    "
    "    "
    "    "
    "    "
    "    "
    " XX "
    " XX ";



}


static void
an_flytext9(frame_xyz F, int frame, void **data)
{
  static const int inter_letter_spacing= 8;
  const char *text= (const char *)*data;
  if ((frame % 2) == 0)
    ef_afterglow(F, 2);

  frame/= 2;
  for (int y= 0; y < SIDE ; ++y)
  {
    if (((y - frame) % inter_letter_spacing) != 0)
      continue;
    int idx= (frame + (SIDE-1-y))/inter_letter_spacing;
    int ch= text[idx % strlen(text)];

    const char *glyph= font9[ch];
    if (!glyph)
      continue;
    int glyph_size= strlen(glyph);
    int glyph_width= glyph_size/9;
    for (int z= 9; z >= 1; --z)
    {
      for (int i= 0; i < glyph_width; ++i)
      {
        if (*glyph++ != ' ')
          F[i+(SIDE-glyph_width)/2][y][z]= 15;
      }
    }
  }
}


static void
an_scrolltext_9(frame_xyz F, int frame, void **data)
{
  const char *text= (const char *)*data;

  /* Find the total text length, in pixels. */
  size_t len = 0;
  const char *p = text;
  while (*p)
  {
    const char *glyph = font9[*p++];
    if (!glyph)
      continue;
    len += strlen(glyph)/9 + 2;
  }

  ef_clear(F);

  /* There are 4*(SIDE-1) positions available. */
  int pos= (frame/2) % (len + 4*(SIDE-1)) - (4*(SIDE-1)-1);

  int cur_pos = -1;
  int cur_glyph_pos = -1;
  int glyph_width = -2;
  int cur_idx = -1;
  const char *cur_glyph;
  for (int i= 0; i < 4*(SIDE-1); ++i, ++pos)
  {
    if (pos < 0 || (size_t)pos >= len)
      continue;
    int x, y;
    if (i < SIDE)
    {
      x= 0;
      y= (SIDE-1) - i;
    }
    else if (i < 2*SIDE-1)
    {
      x= i - (SIDE-1);
      y= 0;
    }
    else if (i < 3*SIDE-2)
    {
      x= (SIDE-1);
      y = i - (2*SIDE-2);
    }
    else
    {
      x= 4*(SIDE-1) - i;
      y= (SIDE-1);
    }
    uint8_t col;
    if (i < 3)
      col= 3 + i*4;
    else if (i > 4*(SIDE-1)-4)
      col= 3 + (4*(SIDE-1)-3-i)*4;
    else
      col= 15;

    /* Find the correct glyph position, possibly moving to the next glyph. */
    while (cur_pos < pos)
    {
      ++cur_pos;
      ++cur_glyph_pos;
      while (cur_glyph_pos >= glyph_width+2)
      {
        cur_idx = (cur_idx + 1) % strlen(text);
        cur_glyph = font9[text[cur_idx]];
        cur_glyph_pos = 0;
        if (cur_glyph)
          glyph_width = strlen(cur_glyph)/9;
        else
          glyph_width = -2;
      }
    }
    for (int z= 0; z < 9; z++)
    {
      if (cur_glyph_pos < glyph_width &&
          cur_glyph[(8-z)*glyph_width + cur_glyph_pos] != ' ')
        F[x][y][z+1]= col;
    }
  }
}


struct st_migrating_dots {
  struct { double x,y,z,v; int target, delay, col, new_col; } dots[SIDE*SIDE];
  /* 0/1 is bottom/top, 2/3 is left/right, 4/5 is front/back. */
  int start_plane, end_plane;
  int base_frame;
  int wait;
  int stage1;
  int text_idx;
};

static const int migrating_dots_col1 = 15;
static const int migrating_dots_col2 = 9;

static int
ut_migrating_dots_get_colour(struct st_migrating_dots *c, int idx,
                             const char *glyph, int glyph_width)
{
  if (!glyph)
    return migrating_dots_col1;
  int x, y;
  switch(c->end_plane)
  {
  case 0:
  case 1:
    if (c->start_plane/2 == 1)
    {
      x = c->dots[idx].target;
      y = (SIDE-1) - c->dots[idx].y;
    }
    else
    {
      x = c->dots[idx].x;
      y = (SIDE-1) - c->dots[idx].target;
    }
    break;
  case 2:
    if (c->start_plane/2 == 0)
    {
      x = (SIDE-1) - c->dots[idx].y;
      y = (SIDE-1) - c->dots[idx].target;
    }
    else
    {
      x = (SIDE-1) - c->dots[idx].target;
      y = (SIDE-1) - c->dots[idx].z;
    }
    break;
  case 3:
    if (c->start_plane/2 == 0)
    {
      x = c->dots[idx].y;
      y = (SIDE-1) - c->dots[idx].target;
    }
    else
    {
      x = c->dots[idx].target;
      y = (SIDE-1) - c->dots[idx].z;
    }
    break;
  case 4:
  case 5:
    if (c->start_plane/2 == 0)
    {
      x = c->dots[idx].x;
      y = (SIDE-1) - c->dots[idx].target;
    }
    else
    {
      x = c->dots[idx].target;
      y = (SIDE-1) - c->dots[idx].z;
    }
    break;
  }
  x = x - (SIDE-glyph_width)/2;
  y = y - (SIDE-9)/2;
  int col;
  if (x < 0 || x >= glyph_width || y < 0 || y >= 9 ||
        glyph[x + glyph_width*y] == ' ')
    col = migrating_dots_col1;
  else
    col = migrating_dots_col2;
  return col;
}

static void
an_migrating_dots(frame_xyz F, int frame, void **data)
{
  static const int start_spread = 12;
  static const double v_min = 0.9;
  static const double v_range = 1.2;
  static const double grav = -0.07;
  static const int stage_pause = 8;
  static const char *text = "LABITAT";

  if (frame == 0)
    *data = malloc(sizeof(struct st_migrating_dots));
  struct st_migrating_dots *c= static_cast<struct st_migrating_dots *>(*data);

  if (frame == 0)
  {
    /* Initialise. */
    c->end_plane = 1;         /* Top; we will copy this to start_plane below. */
    c->wait = stage_pause+1;  /* Will trigger start of new round. */
    c->stage1 = 0;            /* Pretend that we are at the end of stage2. */
    c->text_idx = -5;
    for (int i = 0; i < SIDE*SIDE; ++i)
      c->dots[i].new_col = migrating_dots_col1;
  }

  if (c->wait > stage_pause)
  {
    c->base_frame = frame;
    c->wait = 0;

    if (c->stage1)
    {
      /* Move to stage 2. */
      c->stage1 = 0;
      for (int i = 0; i < SIDE*SIDE; ++i)
      {
        c->dots[i].delay = irand(start_spread);
        if (c->end_plane == 0)
          c->dots[i].v = 0;
        else if (c->end_plane == 1)
          c->dots[i].v = 2 + drand(v_range);
        else
          c->dots[i].v = (2*(c->end_plane%2)-1) * (v_min + drand(v_range));
        c->dots[i].target = (SIDE-1)*(c->end_plane%2);
      }
    }
    else
    {
      /* Start a new round. */
      c->stage1 = 1;

      /* We start where we last ended. */
      c->start_plane = c->end_plane;
      /*
        Choose a plane to move to, but not the same or the one directly
        opposite.
      */
      do
        c->end_plane = irand(6);
      while ((c->end_plane/2) == (c->start_plane/2));

      const char *glyph = c->text_idx >= 0 ? font9[text[c->text_idx]] : NULL;
      ++c->text_idx;
      if (c->text_idx >= strlen(text))
        c->text_idx = -1;           /* -1 => One blank before we start over */
      int glyph_width = glyph ? strlen(glyph)/9 : 0;

      int idx = 0;
      for (int i = 0; i < SIDE; ++i)
      {
        /*
          We will make a random permutation of each row of dots as they migrate
          to a different plane.
        */
        int permute[SIDE];
        for (int j = 0; j < SIDE; ++j)
          permute[j] = j;
        int num_left = SIDE;
        for (int j = 0; j < SIDE; ++j)
        {
          int k = irand(num_left);
          c->dots[idx].target = permute[k];
          permute[k] = permute[--num_left];
          int m = (SIDE-1)*(c->start_plane%2);
          switch (c->start_plane)
          {
          case 0: case 1:
            if (c->end_plane/2 == 1)
            {
              c->dots[idx].y = i;
              c->dots[idx].x = j;
            }
            else
            {
              c->dots[idx].x = i;
              c->dots[idx].y = j;
            }
            c->dots[idx].z = m;
            break;
          case 2: case 3:
            if (c->end_plane/2 == 0)
            {
              c->dots[idx].y = i;
              c->dots[idx].z = j;
            }
            else
            {
              c->dots[idx].z = i;
              c->dots[idx].y = j;
            }
            c->dots[idx].x = m;
            break;
          case 4: case 5:
            if (c->end_plane/2 == 0)
            {
              c->dots[idx].x = i;
              c->dots[idx].z = j;
            }
            else
            {
              c->dots[idx].z = i;
              c->dots[idx].x = j;
            }
            c->dots[idx].y = m;
            break;
          }
          c->dots[idx].delay = irand(start_spread);
          c->dots[idx].col = c->dots[idx].new_col;
          c->dots[idx].new_col =
            ut_migrating_dots_get_colour(c, idx, glyph, glyph_width);
          if (c->start_plane == 1)
            c->dots[idx].v = 0;
          else if (c->start_plane == 0)
            c->dots[idx].v = 2 + v_range;
          else
            c->dots[idx].v = (1-2*(c->start_plane%2)) * (v_min + drand(v_range));
          ++idx;
        }
      }
    }
  }

  int d = frame - c->base_frame;
  int moving = 0;
  for (int i = 0; i < SIDE*SIDE; ++i)
  {
    if (d < c->dots[i].delay)
      continue;

    int plane = c->stage1 ? c->start_plane : c->end_plane;
    double *m;
    switch(plane)
    {
    case 0: case 1:
      m = &c->dots[i].z;
      break;
    case 2: case 3:
      m = &c->dots[i].x;
      break;
    case 4: case 5:
      m = &c->dots[i].y;
      break;
    }

    *m += c->dots[i].v;
    if ((plane % 2 != c->stage1 && *m >= c->dots[i].target) ||
        (plane % 2 == c->stage1 && *m <= c->dots[i].target))
    {
      *m = c->dots[i].target;
    }
    else
    {
      ++moving;
      if (plane <= 1)
        c->dots[i].v += grav;
    }
  }
  if (moving == 0)
    ++c->wait;

  /* Draw the lot. */
  //ef_clear(F);
  ef_afterglow(F, 2);
  for (int i = 0; i < SIDE*SIDE; ++i)
  {
    int x = round(c->dots[i].x);
    int y = round(c->dots[i].y);
    int z = round(c->dots[i].z);
    int col;
    if (c->stage1)
      col = c->dots[i].col;
    else if (d < c->dots[i].delay)
      col = c->dots[i].col + (c->dots[i].new_col - c->dots[i].col)*d/c->dots[i].delay;
    else
      col = c->dots[i].new_col;
    F[x][y][z] = col;
  }
}


static void
an_cosine_plane_N(frame_xyz F, int frame, int side_len, double speed)
{
  ef_afterglow(F, 3);
  for (int i = 0; i <side_len; ++i)
  {
    for (int j = 0; j<side_len; ++j)
    {
      double x = i - ((double)side_len-1)/2;
      double y = j - ((double)side_len-1)/2;
      double r = pow(x*x+y*y, 0.65);
      double z = 0.95*side_len/2.0*cos(0.48*M_PI - frame/speed*M_PI +
                              r/pow(side_len*side_len/4.0, 0.65)*0.6*M_PI);
      int k = round(z + ((double)side_len-1)/2);
      if (k >= 0 && k < side_len)
        F[i][j][k] = 15;
    }
  }
}

static void
an_cosine_plane(frame_xyz F, int frame, void **data)
{
  an_cosine_plane_N(F, frame, SIDE, 30.0);
}

static void
an_cosine_plane5(frame_xyz F, int frame, void **data)
{
  an_cosine_plane_N(F, frame, 5, 22.0);
}


static const int num_quinx = 21;
static const int quinx_duration = 300;
static const double quinx_grav = 0.06;
struct st_quinx {
  struct { double x0,y0,z0,x1,y1,z1,dx0,dy0,dz0,dx1,dy1,dz1; } q[num_quinx];
  int first, count;
  double grav;
};

/*
  Given a point and a direction (velocity), compute the position after the
  next time step, taking into account any (possibly multiple) bounces off
  of a wall.
*/
static void
quinx_bounce(double &x0, double &y0, double &z0, double &dx, double &dy, double &dz)
{
  double x1 = x0 + dx;
  double y1 = y0 + dy;
  double z1 = z0 + dz;

  for (int counter = 0; ; ++counter)
  {
    int dir = 0;
    double dist;

    /* Find the direction that we hit the nearest wall in, if any. */
    if (x1 < -0.5)
    {
      dir = -1;
      dist = (-0.5 - x0)/dx;
    }
    else if (x1 > SIDE - 0.5)
    {
      dir = 1;
      dist = ((SIDE - 0.5) - x0)/dx;
    }
    if (y1 < -0.5 && (!dir || (-0.5 - y0)/dy < dist))
    {
      dir = -2;
      dist = (-0.5 - y0)/dy;
    }
    else if (y1 > SIDE - 0.5 && (!dir || ((SIDE - 0.5) - y0)/dy < dist))
    {
      dir = 2;
      dist = ((SIDE - 0.5) - y0)/dy;
    }
    if (z1 < -0.5 && (!dir || (-0.5 - z0)/dz < dist))
    {
      dir = -3;
      dist = (-0.5 - z0)/dz;
    }
    else if (z1 > SIDE - 0.5 && (!dir || ((SIDE - 0.5) - z0)/dz < dist))
    {
      dir = 3;
      dist = ((SIDE - 0.5) - z0)/dz;
    }

    if (!dir)
      break;

    if (counter >= 3)
      return;                                   // Shouldn't happen.

    /* Bounce of the wall that we hit. */
    switch (dir)
    {
    case -1:
      x1 = 2*(-0.5) - x1;
      dx = -dx;
      break;
    case 1:
      x1 = 2*(SIDE - 0.5) - x1;
      dx = -dx;
      break;
    case -2:
      y1 = 2*(-0.5) - y1;
      dy = -dy;
      break;
    case 2:
      y1 = 2*(SIDE - 0.5) - y1;
      dy = -dy;
      break;
    case -3:
      z1 = 2*(-0.5) - z1;
      dz = -dz;
      break;
    case 3:
      z1 = 2*(SIDE - 0.5) - z1;
      dz = -dz;
      break;
    }
  }
  x0 = x1;
  y0 = y1;
  z0 = z1;
}

static void
an_quinx(frame_xyz F, int frame, void **data)
{
  if (frame == 0)
    *data = malloc(sizeof(struct st_quinx));
  struct st_quinx *c= static_cast<struct st_quinx *>(*data);

  if ((frame % quinx_duration) == 0)
  {
    c->first = 0;
    c->count = 1;
    c->grav = drand(quinx_grav);
    double lx, ly, lz;

    /* Choose end-points, not too close together. */
    do
    {
      c->q[0].x0 = drand(SIDE-1);
      c->q[0].y0 = drand(SIDE-1);
      c->q[0].z0 = drand(SIDE-1);
      c->q[0].x1 = drand(SIDE-1);
      c->q[0].y1 = drand(SIDE-1);
      c->q[0].z1 = drand(SIDE-1);
      lx = c->q[0].x1 - c->q[0].x0;
      ly = c->q[0].y1 - c->q[0].y0;
      lz = c->q[0].z1 - c->q[0].z0;
    } while (lx*lx + ly*ly + lz*lz < 3*3);

    /* Choose directions, not too parallel with line and not too short. */
    for (int i = 0; i < 2; ++i)
    {
      double cos_v, v;
      c->q[0].dx1 = c->q[0].dx0;
      c->q[0].dy1 = c->q[0].dy0;
      c->q[0].dz1 = c->q[0].dz0;
      do
      {
        c->q[0].dx0 = drand(3) - 1.5;
        c->q[0].dy0 = drand(3) - 1.5;
        c->q[0].dz0 = drand(3) - 1.5;
        v = sqrt(c->q[0].dx0*c->q[0].dx0 + c->q[0].dy0*c->q[0].dy0 +
                 c->q[0].dz0*c->q[0].dz0);
        cos_v = (lx*c->q[0].dx0 + ly*c->q[0].dy0 + lz*c->q[0].dz0) /
          sqrt(lx*lx+ly*ly+lz*lz) / v;
      } while (v < 0.7 && cos_v > 0.8);
    }
  }
  else
  {
    /* See if we should add a new line. */
    if ((frame*9/10) != ((frame+1)*9/10))
    {
      int i = (c->first + c->count - 1) % num_quinx;
      int j = (i+1) % num_quinx;

      c->q[j] = c->q[i];
      quinx_bounce(c->q[j].x0, c->q[j].y0, c->q[j].z0,
                   c->q[j].dx0, c->q[j].dy0, c->q[j].dz0);
      quinx_bounce(c->q[j].x1, c->q[j].y1, c->q[j].z1,
                   c->q[j].dx1, c->q[j].dy1, c->q[j].dz1);

      /* Add a bit of gravity. */
      c->q[j].dz0 -= c->grav;
      c->q[j].dz1 -= c->grav;

      if (c->count < num_quinx)
        ++c->count;
      else
        c->first = (c->first + 1) % num_quinx;
    }
  }

  ef_clear(F);
  for (int i = 0; i < c->count; ++i)
  {
    int j = (c->first + i) % num_quinx;
    draw_line(F,
              c->q[j].x0, c->q[j].y0, c->q[j].z0,
              c->q[j].x1, c->q[j].y1, c->q[j].z1,
              5 + 10*(i+1)/c->count
              /*15-10*(num_quinx-i-1)/num_quinx*/);
  }
}


struct st_game_of_life {
  int unchanged;                 // How many iterations have we been unchanged
  int duration;
  int seed_index;
  int p[SIDE*SIDE*SIDE];
  int q[SIDE*SIDE*SIDE];
};

static const int ut_game_of_life_interesting_seeds[] = {
  42, 198, 2439, 216, 242, 2715, 285, 336, 400, 563, 622, 623, 779, 826,
  1406, 1615, 1672, 1763, 2300, 2496, 2641, 2705, 2786, 2909, 3000, 3043, 3196,
  3244, 3312, 3362, 3412, 3627, 3660, 3681, 3692, 4110, 4188, 4558, 4663, 4696,
  5053, 5068, 5129, 5212, 5633, 5886
};
#define SEEDS_END (sizeof(ut_game_of_life_interesting_seeds)/ \
                   sizeof(ut_game_of_life_interesting_seeds[0]))

static void
ut_game_of_life_reset(struct st_game_of_life *c)
{
  if (c->seed_index < SEEDS_END)
    srand(ut_game_of_life_interesting_seeds[c->seed_index++]);
  else
  {
    // When we run out of pre-selected seeds, start looking for new ones.
    if (c->seed_index == SEEDS_END)
      c->seed_index = 1 + ut_game_of_life_interesting_seeds[SEEDS_END - 1];
    //fprintf(stderr, "Next seed: %d prev duration: %d\n", c->seed_index, c->duration);
    srand(c->seed_index++);
  }
  c->unchanged = 0;
  c->duration = 0;
  for (int i= 0; i < SIDE*SIDE*SIDE; i++)
    c->p[i] = (rand() < RAND_MAX/6.6);
}

static void
an_game_of_life(frame_xyz F, int frame, void **data)
{
  if (frame == 0)
    *data = malloc(sizeof(struct st_game_of_life));
  struct st_game_of_life *c= static_cast<struct st_game_of_life *>(*data);

  if (frame == 0)
  {
    c->seed_index = 0;
    ut_game_of_life_reset(c);
  }

  ef_afterglow(F, 3);
  //ef_clear(F);
  if (frame % 8)
  {
    for (int x = 0; x < SIDE; ++x)
      for (int y = 0; y < SIDE; ++y)
        for (int z = 0; z < SIDE; ++z)
          if (c->p[x*SIDE*SIDE+y*SIDE+z])
            F[x][y][z] = 15;
    return;
  }
  int total = 0;
  for (int x = 0; x < SIDE; ++x)
  {
    for (int y = 0; y < SIDE; ++y)
    {
      for (int z = 0; z < SIDE; ++z)
      {
        int neigh = 0;
        for (int i0 = x - 1; i0 <= x + 1; ++i0)
        {
          int i= i0;
          if (i < 0)
            i = SIDE-1;
          else if (i >= SIDE)
            i = 0;
          for (int j0 = y - 1; j0 <= y + 1; ++j0)
          {
            int j= j0;
            if (j < 0)
              j = SIDE-1;
            else if (j >= SIDE)
              j = 0;
            for (int k0 = z - 1; k0 <= z + 1; ++k0)
            {
              int k= k0;
              if (k < 0)
                k = SIDE-1;
              else if (k >= SIDE)
                k = 0;
              if (i == x && j == y && k == z)
                continue;
              if (c->p[i*SIDE*SIDE+j*SIDE+k])
                ++neigh;
            }
          }
        }
        if (!(x >= 0 && x < SIDE && y >= 0 && y < SIDE && z >= 0 && z < SIDE))
          abort();
        if (c->p[x*SIDE*SIDE+y*SIDE+z])
          c->q[x*SIDE*SIDE+y*SIDE+z] = (neigh >= 5 && neigh <= 7);
        else
          c->q[x*SIDE*SIDE+y*SIDE+z] = (neigh >= 6 && neigh <= 6);
        if (c->q[x*SIDE*SIDE+y*SIDE+z])
        {
          ++total;
          F[x][y][z] = 15;
        }
      }
    }
  }
  if (total == 0 || c->duration++ > 58)
    ut_game_of_life_reset(c);
  else if (0 == memcmp(c->p, c->q, sizeof(int)*SIDE*SIDE*SIDE))
  {
    ++c->unchanged;
    if (c->unchanged > 8)
      ut_game_of_life_reset(c);
  }
  else
  {
    c->unchanged = 0;
    memcpy(c->p, c->q, sizeof(int)*SIDE*SIDE*SIDE);
  }
}
#undef SEEDS_END

static void
an_wobbly_plane(frame_xyz F, int frame, int sidelen)
{
  static const double spin_factor = 0.1;
  static const int start_up_down = 250;
  static const double up_down_factor = spin_factor/2;
  static const double wobble_factor = spin_factor/6;
  static const double wobble_amplitude = 0.5;

  ef_clear(F);
  double amp_delta = frame / 100.0;
  if (amp_delta > 0.4)
    amp_delta = 0.4;
  double nx = wobble_amplitude*(amp_delta+fabs(sin(wobble_factor*frame)))*cos(frame * spin_factor);
  double ny = wobble_amplitude*(amp_delta+fabs(sin(wobble_factor*frame)))*sin(frame * spin_factor);
  double nz = 1;
  double x0 = ((double)sidelen -1)/2;
  double y0 = ((double)sidelen -1)/2;
  double z0 = ((double)sidelen -1)/2;
  if (frame >= start_up_down)
    z0 += (double)sidelen/4*sin(frame * up_down_factor);
  draw_plane(x0, y0, z0, nx, ny, nz, F, sidelen);
}

static void
an_wobbly_plane11(frame_xyz F, int frame, void **data)
{
  an_wobbly_plane(F, frame, 11);
}

static void
an_wobbly_plane5(frame_xyz F, int frame, void **data)
{
  an_wobbly_plane(F, frame, 5);
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

  static const int base_count= 10;

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
cornercube_11(frame_xyz F, int frame, void **data)
{
  if (!*data)
    *data= static_cast<void *>(new struct cornercube_data);
  struct cornercube_data *cd= static_cast<struct cornercube_data *>(*data);

  static const int N = 11;
  static const int base_count= 15;

  ef_clear(F);
  if (frame == 0)
  {
    // Initialise first corner to expand from
    cd->col= 0;
    cd->target_col= 15;
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
      cd->target_col= 15;
    }
    double expand_factor= (double)(frame % (base_count+1)) / base_count;
    int col= (int)((double)cd->col +
                   expand_factor * ((double)cd->target_col - (double)cd->col) + 0.5);
    int side_len= (int)(expand_factor * (N-1) + 0.5);
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
        cd->base_x= N-1;
        cd->dir_x= -1;
      }
      else
      {
        cd->base_x= 0;
        cd->dir_x= 1;
      }
      if (corner & 2)
      {
        cd->base_y= N-1;
        cd->dir_y= -1;
      }
      else
      {
        cd->base_y= 0;
        cd->dir_y= 1;
      }
      if (corner & 4)
      {
        cd->base_z= N-1;
        cd->dir_z= -1;
      }
      else
      {
        cd->base_z= 0;
        cd->dir_z= 1;
      }
      // And a new target colour.
      cd->col= cd->target_col;
      cd->target_col= 15;
    }
    double expand_factor= (double)(base_count - (frame % (base_count + 1))) / base_count;
    int col= (int)((double)cd->col +
                   expand_factor * ((double)cd->target_col - (double)cd->col) + 0.5);
    int side_len= (int)(expand_factor * (N-1) + 0.5);
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
    ef_afterglow(F, 2);

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

  if ((cd->count < maxN) && (cd->count == 0 || rand() % 29 == 0))
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


static void
an_rotate_planeN(frame_xyz F, int frame, const char *str_angspeed, int sidelen)
{
  double angspeed = 0.2;
  if (str_angspeed)
  {
    angspeed = atof(str_angspeed);
    if (angspeed <= 0)
      angspeed = 0.2;
  }

  double angle = fmod(frame * angspeed, M_PI) - M_PI/4;
  int orientation = floor(fmod(frame * angspeed, 3*4*2*M_PI) / (4*2*M_PI));
  ef_afterglow(F, 1);
  if (angle <= M_PI/4)
  {
    double slope = tan(angle);
    for (int i = 0; i < sidelen; ++i)
    {
      int a = round(((double)sidelen-1)/2 + slope*((double)i-((double)sidelen-1)/2));
      for (int j= 0; j < sidelen; ++j)
      {
        switch (orientation)
        {
        case 0: F[i][a][j] = 15; break;
        case 1: F[i][j][a] = 15; break;
        case 2: F[j][i][a] = 15; break;
        }
      }
    }
  }
  else
  {
    double slope = 1/tan(angle);
    for (int i = 0; i < sidelen; ++i)
    {
      int a = round(((double)sidelen-1)/2 + slope*((double)i-((double)sidelen-1)/2));
      for (int j= 0; j < sidelen; ++j)
      {
        switch (orientation)
        {
        case 0: F[a][i][j] = 15; break;
        case 1: F[a][j][i] = 15; break;
        case 2: F[j][a][i] = 15; break;
        }
      }
    }
  }
}

static void
an_rotate_plane5(frame_xyz F, int frame, void **data)
{
  an_rotate_planeN(F, frame, (const char *)*data, 5);
}

static void
an_rotate_plane(frame_xyz F, int frame, void **data)
{
  an_rotate_planeN(F, frame, (const char *)*data, 11);
}

/* A plane that scans the 3 directions. */
static void
an_scanplane_N(frame_xyz F, int frame, const char *str_slowdown, int sidelen)
{
  ef_clear(F, 0);
  int slowdown = 5;
  if (str_slowdown)
  {
    slowdown = atoi(str_slowdown);
    if (slowdown < 1)
      slowdown = 5;
  }
  int c = (frame / slowdown) % (3*(2*sidelen-1));
  int direction = c / (2*sidelen-1);
  int pos = c % (2*sidelen-1);
  if (pos >= sidelen)
    pos = 2*sidelen-2-pos;
  for (int i= 0; i < sidelen; ++i)
  {
    for (int j = 0; j < sidelen; ++j)
    {
      switch (direction)
      {
      case 0:
        F[i][j][pos] = 15; break;
      case 1:
        F[i][pos][j] = 15; break;
      case 2:
        F[pos][i][j] = 15; break;
      }
    }
  }
}

static void
an_scanplane5(frame_xyz F, int frame, void **data)
{
  return an_scanplane_N(F, frame, (const char *)*data, 5);
}

static void
an_scanplane(frame_xyz F, int frame, void **data)
{
  return an_scanplane_N(F, frame, (const char *)*data, SIDE);
}


/*
  An animation where, in the 11x11x11 cube, we successively add up to 8
  different 5x5x5 cube-animations, running each in their own corner!
*/

struct st_cube5_times_8 {
  int num_anims;
  int frame_counter[8];
  struct {frame_xyz f;} framebuffers[8];
  int offx[8];
  int offy[8];
  int offz[8];
};

static struct {int x,y,z;} corners_cube5_times_8[7] = {
  { 1,1,1 },
  { 0,1,0 },
  { 1,1,0 },
  { 0,1,1 },
  { 1,0,1 },
  { 0,0,1 },
  { 1,0,0 }
};
static struct anim_piece animations_cube5_times_8[] = {
  { an_wobbly_plane5, 0, 0 },
  { an_rotate_plane5, 0, (void *)"0.14" },
  { an_scanplane5, 0, (void *)"3" },
  { scrolltext_labitat_5, 0, 0 },
  { scrolltext_SiGNOUT_5, 0, 0 },
  { bubble5_a, 0, 0 },
  { cornercube_5, 0, 0 },
  { an_flytext5, 0, (void *)" LABITAT  " },
};

static void
an_cube5_times_8(frame_xyz F, int frame, void **data)
{
  static const int new_piece = 235;
  static const int move_in_place_time = 15;

  if (frame == 0)
    *data = malloc(sizeof(struct st_cube5_times_8));
  struct st_cube5_times_8 *c = static_cast<struct st_cube5_times_8 *>(*data);

  if (frame == 0)
  {
    c->num_anims = 0;
  }

  /* Add a new piece? */
  if (c->num_anims < 8 && (frame % new_piece) == 0)
  {
    int i = c->num_anims++;
    c->frame_counter[i] = 0;
    ef_clear(c->framebuffers[i].f);
    c->offx[i] = 0;
    c->offy[i] = 0;
    c->offz[i] = 0;
  }
  /* Move in place? */
  if (frame < 7 * new_piece &&
      (frame % new_piece) >= new_piece - move_in_place_time)
  {
    int i = c->num_anims - 1;
    int pos = (frame % new_piece) - (new_piece - move_in_place_time);
    c->offx[i] = corners_cube5_times_8[i].x*6*pos/(move_in_place_time-1);
    c->offy[i] = corners_cube5_times_8[i].y*6*pos/(move_in_place_time-1);
    c->offz[i] = corners_cube5_times_8[i].z*6*pos/(move_in_place_time-1);
  }

  ef_clear(F);
  for (int i = 0; i < c->num_anims; ++i)
  {
    /* Run the individual animation one frame. */
    struct anim_piece *an = &animations_cube5_times_8[i];
    (*an->frame_func)(c->framebuffers[i].f, (c->frame_counter[i])++, &an->data);
    /* Copy the individual animation into place. */
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y)
        for (int z = 0; z < 5; ++z)
          F[x+c->offx[i]][y+c->offy[i]][z+c->offz[i]] =
            c->framebuffers[i].f[x][y][z];
  }
}


static void
an_stripes(frame_xyz F, int frame, void **data)
{
  static const double rate = 0.5;
  static const int N = 13;

  for (int i = 0; i < SIDE; ++i)
  {
    for (int j = 0; j < SIDE; ++j)
    {
      for (int k = 0; k < SIDE; ++k)
      {
        int d = (i+j+k+(int)round(frame*rate)) % (2*N-1);
        int col;
        if (d <= N)
          col = d + (15-N);
        else
          col = (2*N-1)+15-d;
        F[i][j][k] = col;
      }
    }
  }
}


static void
an_stripe_ball(frame_xyz F, int frame, void **data)
{
  static const double rate = 0.5;
  static const double width = 4.2;
  static const int N = 15;

  for (int i = 0; i < SIDE; ++i)
  {
    for (int j = 0; j < SIDE; ++j)
    {
      for (int k = 0; k < SIDE; ++k)
      {
        double x = i - (SIDE-1)/2.0;
        double y = j - (SIDE-1)/2.0;
        double z = k - (SIDE-1)/2.0;
        double r = sqrt(x*x+y*y+z*z);
        int d = ((int)round(1e9 - r/width*N + frame*rate)) % (2*N-1);
        int col;
        if (r > 0.5 + (SIDE-1)/2.0)
          col = 0;
        else if (d <= N)
          col = d + (15-N);
        else
          col = (2*N-1)+15-d;
        F[i][j][k] = col;
      }
    }
  }
}


struct st_smoketail {
  int idx;
  struct { double x, y, z; } p[150];
};

static void
an_smoketail(frame_xyz F, int frame, void **data)
{
  static const int steps = 13;
  static const int stepsize = 8;

  if (frame == 0)
    *data = malloc(sizeof(struct st_smoketail));
  struct st_smoketail *c= static_cast<struct st_smoketail *>(*data);

  if (frame == 0)
    c->idx = 0;

  double u = frame/353.23*2*M_PI;
  double v = frame/147.2*2*M_PI;
  double h = sin(u);
  double r = sqrt(1.0 - h*h);
  double ax = r*cos(v);
  double ay = r*sin(v);
  double az = h;

  double x = 5;
  double y = 0;
  double z = 0;
  ut_rotate_axis(ax, ay, az, &x, &y, &z, (double)frame/150.0*2*M_PI);
  x += 5;
  y += 5;
  z += 5;

  c->p[c->idx].x = x;
  c->p[c->idx].y = y;
  c->p[c->idx].z = z;

  ef_clear(F);
  draw_line(F, 5, 5, 5, round(5+5*ax), round(5+5*ay), round(5+5*az), 10);
  draw_line(F, 5, 5, 5, round(x), round(y), round(z), 15);

  int max_l = 0;
  for (int i = 0; i < SIDE; ++i)
  {
    for (int j = 0; j < SIDE; ++j)
    {
      for (int k = 0; k < SIDE; ++k)
      {
        int max_col = 0;
        int idx = c->idx;
        for (int l = 0; l < steps && l*stepsize <= frame ; ++l)
        {
          if (max_l < l)
            max_l = l;
          double x = c->p[idx].x;
          double y = c->p[idx].y;
          double z = c->p[idx].z;
          double r = sqrt((i-x)*(i-x) + (j-y)*(j-y) + (k-z)*(k-z));
          int col = round(16.5 - pow(r, 0.67) * 4.5 - 13*((double)l/steps));
          if (col > 15)
            col = 15;
          if (col > max_col)
            max_col = col;
          idx -= stepsize;
          if (idx < 0)
            idx += sizeof(c->p)/sizeof(c->p[0]);
        }
        F[i][j][k] = max_col;
      }
    }
  }

  c->idx = (c->idx+1) % (sizeof(c->p)/sizeof(c->p[0]));
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
  ef_clear(F, 0);
  //for (int i = 0; i < SIDE; ++i) for (int j= 0; j < SIDE; ++j) F[i][j][(frame/50)%SIDE] = 15;
  /*
  if ((frame / 50)%2)
    ef_clear(F, 15);
  else
    ef_clear(F, 0);
  */
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

static void
testimg_test_lines(frame_xyz F, int frame, void **data)
{
  ef_clear(F, 0);
  draw_line(F, 2, 2, 2, 10, 3, 5, (frame/2)%16);
  draw_line(F, 3, 10, 5, 2, 2, 2, (frame/2+10)%16);
  draw_line(F, 3, 5, 10, 2, 2, 2, (frame/2+20)%16);
}

static void
testimg_test_column(frame_xyz F, int frame, void **data)
{
  ef_clear(F);
  F[SIDE-1-((frame/(8*SIDE*SIDE))%SIDE)][(frame/(8*SIDE))%SIDE][(frame/8)%SIDE] = 11;
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
    for (int x= 0; x < SIDE; ++x)
      for (int y= 0; y < SIDE; ++y)
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


static struct anim_piece animation5[] = {
  // { testimg_x_axis, 100000, 0},
  // { testimg_y_axis, 100000, 0},
  // { testimg_z_axis, 100000, 0},
  // { testimg_solid, 100000, 0},
  // { testimg_rect5, 100000, 0},
  // { testimg_walk_bottom_5, 100000, 0},
  // { testimg_show_greyscales_5, 100000, 0},
  // { testimg_show_greyscales_bottom_5, 100000, 0},
  { an_cosine_plane5, 600, 0 },
  { fade_out, 16, 0 },
  { an_wobbly_plane5, 800, 0 },
  { fade_out, 16, 0 },
  { an_rotate_plane5, 900, (void *)"0.14" },
  { fade_out, 16, 0 },
  { an_scanplane5, 600, (void *)"3" },
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

static struct anim_piece animation[] = {
  //{ testimg_test_lines, 100000, 0 },
  //{ testimg_test_column, 100000000, 0 },
  //{ testimg_solid, 1000000, 0 },
  { cornercube_11, 300, 0 },
  { fade_out, 16, 0 },
  { an_scrolltext_9, 1000, (void *)"LOREM IPSUM DOLOR SIT AMET OG NOGET MED BATTERIER" },
  { fade_out, 16, 0 },
//  { an_scrolltext_9, 950, (void *)"CHECK WWW.HEADRC.COM FOR FREE LIPO CELLS!" },
//  { fade_out, 16, 0 },
//  { an_scrolltext_9, 850, (void *)"HYPERION - REALLY GOOD LIPO CELLS!" },
//  { fade_out, 16, 0 },
//  { an_scrolltext_9, 850, (void *)"LINUS" },
//  { fade_out, 16, 0 },
  { an_scrolltext_9, 850, (void *)"LABITAT" },
  { fade_out, 16, 0 },
  { an_smoketail, 1400, 0 },
  { an_wireframe, 1575, 0 },
  { fade_out, 16, 0 },
  { an_stripe_ball, 900, 0 },
  { an_fireworks, 1200, 0 },
  { fade_out, 16, 0 },
  { an_migrating_dots, 1800, 0 },
  { fade_out, 16, 0 },
  { an_wobbly_plane11, 900, 0 },
  { fade_out, 16, 0 },
  { an_stripes, 1100, 0 },
  { an_flytext9, 800, (void *)" LABITAT" },
  { fade_out, 16, 0 },
  { an_fountain, 900, 0 },
  { fade_out, 16, 0 },
  { an_cube5_times_8, 2300, 0 },
  { fade_out, 16, 0 },
  { an_quinx, 1500, 0 },
  { fade_out, 16, 0 },
  { an_game_of_life, 3200, 0 },
  { fade_out, 16, 0 },
  { an_cosine_plane, 900, 0 },
  { fade_out, 16, 0 },
  { an_rotate_plane, 900, (void *)"0.17" },
  { an_scrolltext_9, 850, (void *)"LABITAT" },
  { an_scanplane, 600, (void *)"2" },
  { 0, 0, 0}
};


int
main(int argc, char *argv[])
{
  init_font5();
  init_font9();

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
  //play_animation(animation5);
  play_animation(animation);
  return 0;
}
