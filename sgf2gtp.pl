#!/usr/bin/perl -l
# This is a naive Perl script that will convert SGF files to GTP
# format so that you can feed them to Pachi, insert genmove at
# the right places etc. Might not work on obscure SGF files,
# and of course there must be no variations.

use warnings;

local $/ = undef; my $sgf = <>;
my $size = ($sgf =~ /SZ\[(\d+)\]/)[0];
$sgf =~ s/\bC\[.*?\]//gs; # no comments
#$sgf =~ s/\).*//gs; # cut at end of principal branch

print "boardsize " . $size;
print "clear_board";
print "komi " . ($sgf =~ /KM\[([\d.]+)\]/)[0];

my $abcd = "abcdefghijklmnopqrstuvwxyz";

my @m = split /;/, $sgf;
foreach (@m) {
	/^([BW])\[\]/ and print "play $1 pass";
	/^([BW])\[(\w\w)\]/ or next;
	my ($color, $coord) = ($1, $2);
	my ($x, $y) = split //, $coord;
	($x ge 'i') and $x++;
	$y = $size - index($abcd, $y);
	print "play $color $x$y";
}
