#! /usr/bin/perl

use strict;
use warnings;

open FH, '+<', '/dev/ttyUSB0' or die "open() failed: $!\n";

select FH; $| = 1; select STDOUT;
binmode FH;

# Frame format:
#
# <0> <frame counter 0-63> <len_low> <len_high> <data> <checksum> <0xff>
# Data is 1337 leds * N bits/led, rounded up to next byte.

#my $clunk = [0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01, 0x00];
#my $clunk = [0xff, 0xff, 0xff];

my $frame = 0;
for (;;) {
#  my $x = $clunk->[int($frame / 10) % scalar(@$clunk)];

  my $N = 4;
  my $len = int ((1331*$N + 7)/8);
  my $len_low = $len & 0xff;
  my $len_high = $len >> 8;
  my $data = chr(0) . chr($frame % 64) . chr($len_low) . chr($len_high);
  my $checksum = 0;
  for my $i (0 .. $len-1) {
#    my $x = ((2*$i+int($frame/10)) % 16) << 4 | ((2*$i+int($frame/10)+1) % 16);
#    my $x = ($i % 3 == 2) ? 0xaf : ($i % 3 ? 0x37 : 0x01);
    my $x = int($frame / 20) % 16; $x = 15 - $x if int($frame / (20*16)) % 2; $x = $x | $x << 4;
    $checksum = $checksum ^ (($x + $i) & 0xff);
    $data = $data . chr($x);
  }
  $data = $data . chr($checksum) . chr(0xff);
  print FH $data;
  ++$frame;
  my $rin = '';
  vec($rin, fileno(FH), 1) = 1;
  my $res = select($rin, undef, undef, 0);
  if ($res > 0) {
    my $y = '';
    sysread(FH, $y, 128);
    print ord(substr($y, $_, 1)), " " for (0 .. length($y)-1);
    print "FRAME: sent ", $frame % 64, " recv ", ord(substr($y, -1)), "\n";
  }
  #select(undef, undef, undef, 0.130);
}
