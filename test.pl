#! /usr/bin/perl

use strict;
use warnings;

use Device::SerialPort;

sub mk_frame {
  my ($data) = @_;
  my $checksum = 0;
  my $quoted = chr(0xfd);
  for (my $i = 0; $i < length($data); $i++) {
    my $x = substr($data, $i, 1);
    my $n = ord($x);
    $checksum ^= $n;
    if ($n == 0xfd || $n == 0xfe) {
      $quoted .= chr(0xfe) . chr($n - 0xfd);
    } else {
      $quoted .= $x;
    }
  }
  $quoted .= chr($checksum);
  return $quoted;
}

sub blocking_write {
  my ($port, $data) = @_;

  my $sofar = 0;
  my $len = length($data);
  while ($sofar < $len) {
    my $win = '';
    vec($win, fileno($port),1) = 1;
    select(undef, $win, undef, 0);
    $sofar += syswrite($port, $data, $len - $sofar, $sofar);
  }
}

my $port = Device::SerialPort->new("/dev/ttyUSB0");
if (!$port) {
  die "Failed to open serial device: $!\n";
}

$port->baudrate(115200);
$port->parity("none");
$port->handshake("none");
$port->databits(8);
$port->stopbits(1);

my $frame_length = int((1337+7)/8);

my $frame1 = mk_frame(chr(0x55) x $frame_length);
my $frame2 = mk_frame(chr(0xaa) x $frame_length);

print "Frame length: ", length($frame1), "\n";
for (;;) {
  print scalar(localtime()), "\n";
  for (1..25) {
    blocking_write($port, $frame1);
  }
  for (1..25) {
    blocking_write($port, $frame2);
  }
}
