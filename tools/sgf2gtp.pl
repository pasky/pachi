#!/usr/bin/perl -l
#
# sgf2gtp - Convert SGF game record to GTP command stream
#
# Usage: sgf2gtp [-g] [-n MOVENUM] [FILENAME]
#
# This is a naive Perl script that will convert SGF files to GTP
# format so that you can feed them to Pachi, insert genmove at
# the right places etc. Might not work on obscure SGF files,
# and of course there must be no variations.
#
# When called with a filename argument, it will create the output
# file with .gtp extension instead of .sgf.
#
# -g: Automatically append genmove command for the other color.
# -n MOVENUM: Output at most first MOVENUM moves.

use warnings;

my $genmove;
if ($ARGV[0] and $ARGV[0] eq '-g') {
	shift @ARGV;
	$genmove = 1;
}

my $maxmoves;
if ($ARGV[0] and $ARGV[0] eq '-n') {
	shift @ARGV;
	$maxmoves = shift @ARGV;
}

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
my $movenum = 0;
my $last_color = 'w';

my @m = split /;/, $sgf;
foreach (@m) {
	$maxmoves and $movenum >= $maxmoves and last;

	if (/^([BW])\[\]/) {
		$last_color = $1;
		$movenum++;
		print "play $1 pass";
		next;
	}
	unless (/^([BW])\[(\w\w)\]/) {
		next;
	}

	my ($color, $coord) = ($1, $2);
	my ($x, $y) = split //, $coord;
	($x ge 'i') and $x++;
	$y = $size - index($abcd, $y);

	$last_color = $color;
	$movenum++;
	print "play $color $x$y";
}

if ($genmove) {
	print "genmove ".(uc $last_color eq 'W' ? 'B' : 'W');
}
