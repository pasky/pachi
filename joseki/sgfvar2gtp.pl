#!/usr/bin/perl
# Convert SGF file to a sequence of GTP games, one game per each variation
# that ends with /GOOD/ comment.

use warnings;
use strict;

sub printgame
{
	my ($sgf) = @_;
	my $pt = $sgf->getAddress();

	my @moves;
	do {
		my ($b, $w) = ($sgf->property('B'), $sgf->property('W'));
		if ($b) { push @moves, ['b', $_] foreach @$b; }
		if ($w) { push @moves, ['w', $_] foreach @$w; }
	} while ($sgf->prev());

	print "boardsize 19\nclear_board\n";
	for my $move (reverse @moves) {
		my ($sx, $sy) = @{$move->[1]};
		my @abcd = split(//, "abcdefghjklmnopqrstuvwxyz");
		my $x = $sy + 1; my $y = $abcd[18 - $sx];
		if ("$y$x" eq "z20") {
			$y = "pass"; $x = "";
		}
		print "play ".$move->[0]." $y$x\n";
	}

	$sgf->goto($pt);
}

sub recurse
{
	my ($sgf) = @_;
	my $c = $sgf->property('C');
	if ($c and $c->[0] =~ /GOOD/) {
		printgame($sgf);
	}
	for (0 .. $sgf->branches()-1) {
		$sgf->gotoBranch($_);
		recurse($sgf);
		$sgf->prev();
	}
}

use Games::SGF::Go;
my $sgf = new Games::SGF::Go;

$sgf->readFile($ARGV[0]);

$sgf->gotoRoot();
recurse($sgf);
