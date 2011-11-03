#! /usr/bin/perl

use strict;
use warnings;

open FH, '+<', '/dev/ttyUSB0' or die "open() failed: $!\n";

select FH; $| = 1; select STDOUT;

# Frame format:
#
# <0> <frame counter 0-63> <len_low> <len_high> <data> <checksum> <0xff>
# Data is 1337 leds * N bits/led, rounded up to next byte.

my $frame = 0;
for (;;) {
  my $x;
  if (int($frame / 25) % 2) {
    $x = 0xaa;
  } else {
    $x = 0x55;
  }
  if (!($frame % 25)) {
    print "$x\n";
  }
  my $N = 1;
  my $len = int ((1337*$N + 7)/8);
  my $len_low = $len & 0xff;
  my $len_high = $len >> 8;
  my $data = chr(0) . chr($frame % 64) . chr($len_low) . chr($len_high);
  my $checksum = 0;
  for my $i (0 .. $len-1) {
    $checksum = $checksum ^ ($x + $i);
  }
  $data = $data . chr($x) x $len . chr($checksum) . chr(0xff);
  print FH $data;
  ++$frame;
}
