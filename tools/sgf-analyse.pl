#!/usr/bin/perl
# Simple script using Pachi for game analysis of SGF.
# Pachi will analyse each move in the SGF and suggest the winrate
# for either player and winrate it estimated for in-game followup.
#
# Usage: tools/sgf-analyse.pl COLOR SGF PACHIARGS...
#
# Note that this script assumes dynkomi=none and does not show
# dynamic komi information. (Would be trivial to add but it's tricky
# to interpret the data.)
#
# This script is dirty and insecure for untrusted input!
#
# Example: tools/sgf-analyse.pl W progame.sgf -t =2000 -d 0 dynkomi=none
# ...to get 2000 simulations per move, and winrates from white perspective.
#
# To plot the output in gnuplot:
# 	set yr [0:1]
# 	set ytics 0,0.25,1
# 	plot "datafile.csv" using 1:5 with lines

use warnings;
use strict;

my $mcolor = shift @ARGV;
my $sgf = shift @ARGV;

sub one {
	my ($move) = @_;

	# Move of the other color - supposedly.
	return if ($move % 2 == ($mcolor eq 'B' ? 0 : 1));

	# Get pachi output from GTP stream that contains the SGF up to
	# given move, with information about the originally made move
	# included.
	my $line = $move + 3; # board_size, clearboard, komi
	my $rest = $line + 1;
	open my $g, "tools/sgf2gtp.pl < \"$sgf\" | sed -e '$line s/play \\(.*\\) \\(.*\\)/1 echo \\1 \\2\\n2 genmove \\1\\n3 pachi-result/' -e '$rest,\$ d' | ./pachi @ARGV |" or die $!;

	# Parse the GTP output.
	my ($color, $realmove, $genmove, $winrate) = @_;
	while (<$g>) {
		chomp;
		if (/^=1 (.*) (.*)/) {
			$color = $1; $realmove = uc $2;
		} elsif (/^=2 (.*)/) {
			$genmove = $1;
		} elsif (/^=3 (.*) (.*) (.*) (.*)/) {
			$winrate = $mcolor eq $color ? $3 : 1.0 - $3;
		}
	}

	# Pass value is not interesting since Pachi might want
	# to clarify some groups yet.
	return if $realmove eq 'PASS';

	# Generate summary line.
	print join(', ', $move, $color, $realmove, $genmove, $winrate) . "\n";
}

print "# $sgf @ARGV\n";

my $moves = `tools/sgf2gtp.pl < \"$sgf\" | wc -l` - 3;
one($_) for (1 .. $moves);
