#! /usr/bin/perl

use strict;
use warnings;

use Time::HiRes;
use Fcntl;

my $FRAMERATE= 50;

my $device = $ARGV[0] || '/dev/ttyUSB0';

open FH, '+<', $device or die "open() failed: $!\n";

select FH; $| = 1; select STDOUT;
binmode FH;

my $N = 4;
my $len = 6 + int ((11**3*$N + 7)/8);
my $inter_frame_delay= 1.0/$FRAMERATE;

# For some reason, select(..., 0) and non-blocking sysread() on the
# Arduino USB-to-Serial occasionally (every 10 seconds) stalls for ~0.2 seconds.
# Work-around this by forking a child to do the read and proxy it onto
# a pipe which does not stall.
my $child= open(PIPE, '-|');
if (!defined($child)) {
  die "fork() failed";
} elsif (!$child) {
  # Child.
  close (STDIN);
  $|= 1;
  for (;;) {
    my $b;
    if (sysread(FH, $b, 1))
    {
      print $b;
    } else {
      die "Child: read failed: $!\n";
    }
  }
} else {
  my $flags= fcntl(PIPE, F_GETFL, 0);
  die "Get fcntl flags failed" unless $flags;
  fcntl(PIPE, F_SETFL, $flags|O_NONBLOCK)
      or die "Set fcntl O_NONBLOCK flag failed";
}

my $cnt= 0;
my $last_cnt = -1;
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
  my $y = '';
  my $res= sysread(PIPE, $y, 4096);
  if ($res) {
    my $err= undef;
    for (0 .. length($y)-1) {
      my $ack = ord(substr($y, $_, 1));
      #print $ack, " ";
      if ($ack & 0x80) {
        print "ERROR: ", $ack & 0x3f, "\n";
        $err= 1;
      }
    }
    if ($err) {
        # just pause for a bit, then cube will reset serial, so this is a cheap
        # hack to get back in sync.
        sleep 1;
        # Flush any queued-up errors.
        my $dummy;
        sysread(PIPE, $dummy, 4096);
    }
    if (($last_cnt % 64) > ($cnt % 64))
    {
      #print "FRAME: sent ", $cnt % 64, " recv ", ord(substr($y, -1)), "\n";
    }
    $last_cnt = $cnt;
  }

  my $cur_time= [Time::HiRes::gettimeofday()];
  my $elapsed= Time::HiRes::tv_interval($old_time, $cur_time);
  if ($elapsed < $inter_frame_delay) {
    #print "Throttle! ", $inter_frame_delay - $elapsed, " seconds\n";
    Time::HiRes::sleep($inter_frame_delay - $elapsed);
  }
}
