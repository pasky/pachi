#!/usr/bin/perl
# ./pattern3_show.pl PATNUM

use warnings;
no warnings "qw";
use strict;

my $pat = shift @ARGV;
my @s = qw(. X O #);

my @p = map { ($pat >> (2*$_)) & 3 } (0..7);
splice @p, 4, 0, (0);
for my $y (0..2) {
	for my $x (reverse 0..2) {
		print $s[$p[$x + $y*3]];
	}
	print "\n";
}
