#!/usr/bin/perl -l
# This is a naive Perl script that will convert SGF files to GTP
# format so that you can feed them to Pachi, insert genmove at
# the right places etc. Might not work on obscure SGF files,
# and of course there must be no variations.

use warnings;

local $/ = undef; my $sgf = <>;

print "boardsize " . ($sgf =~ /SZ\[(\d+)\]/)[0];
print "komi " . ($sgf =~ /KM\[([\d.]+)\]/)[0];
print "clear_board";

my @m = split /;/, $sgf;
foreach (@m) {
	/^([BW])\[(\w\w)\]/ or next;
	my ($color, $coord) = ($1, $2);
	$coord =~ s/i/j/;
	my ($x, $y) = split //, $coord;
	$y =~ tr/abcdefghj/987654321/;
	print "play $color $x$y";
}
