#!/usr/bin/perl -l
# This is a naive Perl script that will convert SGF files to GTP
# format so that you can feed them to Pachi, insert genmove at
# the right places etc. Might not work on obscure SGF files,
# and of course there must be no variations.
#
# When called with a filename argument, it will create the output
# file with .gtp extension instead of .sgf.

use warnings;

if ($ARGV[0]) {
	open STDIN, "$ARGV[0]" or die "$ARGV[0]: $!";
	my $ofile = $ARGV[0]; $ofile =~ s/sgf$//i; $ofile .= 'gtp';
	open STDOUT, ">$ofile" or die "$ofile: $!";
}

local $/ = undef; my $sgf = <>;
my $size = ($sgf =~ /SZ\[(\d+)\]/)[0];
$size ||= 19;
$sgf =~ s/\bC\[.*?\]//gs; # no comments
#$sgf =~ s/\).*//gs; # cut at end of principal branch

print "boardsize " . $size;
print "clear_board";
if ($sgf =~ s/\bKM\[([\d.]+)\]//gs) {
	print "komi $1";
}
if ($sgf =~ s/\bHA\[(\d+)\]//gs and $1 > 0) {
	print "fixed_handicap $1";
}

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
