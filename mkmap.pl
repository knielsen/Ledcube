#! /usr/bin/perl

use strict;
use warnings;

# Compute the lookup-table for ledcube.c, given the pinout from the
# PCB layoyt.
#
# Note that for this one, j4 was moved from IC6 pin3 to IC8 pin 15
# (patching of bad soldering).

my @a = qw/d10 c10 b10 d9 a10 c9 b9 a9 b8 c8 d8 a8 a7 b7 c7 d7
a6 a5 a4 b6 b5 b4 c4 c5 d4 d5 c6 e5 d6 f7 e6 e7
a3 a2 a1 b3 a0 b2 b1 b0 c0 c1 c3 c2 d0 d1 d2 d3
e0 f0 g0 e1 f1 g1 g2 f2 g3 g4 e2 f3 f4 e3 e4 f5
h0 i0 j0 h1 k0 i1 j1 k1 k2 j2 h2 i2 k3 j3 i3 h3
k4 k5 k6 X j5 j6 i6 h6 i5 g7 i4 h5 g6 h4 g5 f6
k7 k8 k9 j7 k10 j8 j9 j10 i10 i9 i7 i8 h10 h9 h8 h7
g10 f10 e10 g9 f9 e9 e8 f8 g8 X X X X X X j4/;
@a = reverse @a;

my $i = 0;
for (@a) {
  my $v;
  if ($_ eq 'X') {
    $v = "0x8000";
  } else {
    my $x = ord(substr($_, 0,1)) - ord('a');
    my $y = 0 + substr($_, 1);
    $v = (10-$x)*11+$y;
  }
  print $v, ",";
  ++$i;
  print $i % 8 == 0 ? "\n" : " ";
}
