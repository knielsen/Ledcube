#! /usr/bin/perl

use strict;
use warnings;

# Compute lookup-tables for generating the "cosine plane" animation on-board
# the LED cube.
#
# The table is used to generate the following formulae:
#
# z = round(0.95*11/2 * cos(0.48 PI - F/30*PI) + 0.6 PI (x**2+y**2)**0.65/ (11*11/4)**0.65))
#
# Here,
#   x,y,z   coordinates [-5..5]
#   F       frame counter 50 Hz
#
# We first look up an intermediary value W as one uint8_t for the argument to
# cos(), so that 256 corresponds to 2*PI. This lookup table needs 51 entries,
# from 0 (x=y=0) to 50 (x=y=5).
#
# We then look up z in a second table with 256 entries.

my $M_PI = 3.141592654;

sub round {
  my $x = shift;
  return int($x >= 0 ? $x + 0.5 : $x - 0.5);
}


sub out_array {
  my ($name, $arr) = @_;

  my $n = scalar(@$arr);

  print "static const uint8_t\n${name}[] PROGMEM = {\n";
  for my $i (0 .. $n-1) {
    if ($i % 8 == 0) {
      print "  ";
    }
    print $arr->[$i];
    if ($i != ($n-1)) {
      print ",";
      if (($i+1) % 8 == 0) {
        print "\n";
      }
    }
  }
  print "\n};\n";
}

sub mk_cosplane_lookup_r2angle {
  my $arr = [0..50];
  for my $i (0..50) {
    my $val = 0.6*$M_PI * ($i**0.65)/((11*11/4)**0.65);
    $arr->[$i] = round($val/(2*$M_PI)*256) % 256;
  }
  return $arr;
}

sub mk_cosplane_lookup_angle2z {
  my $arr = [0..255];
  for my $i (0 .. 255) {
    my $val = round(0.95*11/2*cos($i/256*2*$M_PI));
    $val = -5 if $val < -5;
    $val = 5 if $val > 5;
    $arr->[$i] = $val + 5;
  }
  return $arr;
}

out_array("cosplane_lookup_r2angle", mk_cosplane_lookup_r2angle());
print "\n";
out_array("cosplane_lookup_angle2z", mk_cosplane_lookup_angle2z());
print "\n";

print <<END;
static inline uint8_t
cosplane_get_k(int8_t i, int8_t j, uint8_t frame_counter)
{
  i -= 5;
  j -= 5;
  int8_t r = (i*i + j*j);
  uint8_t angle = pgm_read_byte(&cosplane_lookup_r2angle[r]);
  /* Frame counter goes from 0..209 and then wraps. */
  angle += 1 + ~((uint8_t) (((uint16_t)frame_counter*(uint16_t)312) >> 8));
  angle += 61;
  return pgm_read_byte(&cosplane_lookup_angle2z[angle]);
}
END
