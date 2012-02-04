#! /usr/bin/perl

use strict;
use warnings;

use Time::HiRes;

my $FRAMERATE= 25;

open FH, '+<', '/dev/ttyUSB0' or die "open() failed: $!\n";

select FH; $| = 1; select STDOUT;
binmode FH;

my $N = 4;
my $len = 6 + int ((11**3*$N + 7)/8);
my $inter_frame_delay= 1.0/$FRAMERATE;

my $cnt= 0;
for (;;) {
  my $old_time= [Time::HiRes::gettimeofday()];
  my $sofar= 0;
  my $buf;
  while ($sofar < $len) {
    $buf= '';
    my $res= sysread(STDIN, $buf, $len - $sofar);
    if (!defined($res)) {
      die "Error reading: $!\n";
    } elsif (!$res) {
      if (seek(STDIN, 0, 0)) {
        # File ended, loop back to the start.
        next;
      }
      print "EOF on input, exiting\n";
      exit 0;
    }
    print FH $buf;
    $sofar+= $res;
  }
  ++$cnt;

  # Check what arduino reports back
  my $rin = '';
  vec($rin, fileno(FH), 1) = 1;
  my $res = select($rin, undef, undef, 0);
  if ($res > 0) {
    my $y = '';
    sysread(FH, $y, 128);
    print ord(substr($y, $_, 1)), " " for (0 .. length($y)-1);
    print "FRAME: sent ", $cnt % 64, " recv ", ord(substr($y, -1)), "\n";
  }

  my $cur_time= [Time::HiRes::gettimeofday()];
  my $elapsed= Time::HiRes::tv_interval($old_time, $cur_time);
  if ($elapsed < $inter_frame_delay) {
    Time::HiRes::sleep($inter_frame_delay - $elapsed);
  }
}
