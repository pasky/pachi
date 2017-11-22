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

my $abcd = "abcdefghijklmnopqrstuvwxyz";
my $size = 19;

# Convert sgf coord to gtp
sub sgf2gtp
{
    my ($coord) = @_;
    my ($x, $y) = split //, $coord;
    ($x ge 'i') and $x++;
    $y = $size - index($abcd, $y);
    return "$x$y";
}

my $genmove;
if ($ARGV[0] and $ARGV[0] eq '-g') {  shift @ARGV;  $genmove = 1;  }

my $maxmoves;
if ($ARGV[0] and $ARGV[0] eq '-n') {  shift @ARGV;  $maxmoves = shift @ARGV;  }

if ($ARGV[0]) {
	open STDIN, "$ARGV[0]" or die "$ARGV[0]: $!";
	my $ofile = $ARGV[0]; $ofile =~ s/sgf$//i; $ofile .= 'gtp';
	open STDOUT, ">$ofile" or die "$ofile: $!";
}

local $/ = undef; my $sgf = <>;
$sgf =~ s/\\]//gs;        # remove escaped brackets (in comments)
$sgf =~ s/\bC\[.*?\]//gs; # no comments
#$sgf =~ s/\).*//gs; # cut at end of principal branch

my @m = split(/;/, $sgf);
if (shift @m ne "(") {  die "doesn't look like valid sgf, aborting\n";  }
my $h = shift @m;    # game header
$h =~ s/[ \n\t]//gs; # no whitespaces

if ($h =~ /SZ\[(\d+)\]/) {  $size = $1;  } else {  warn "WARNING: no board size in game header ...\n";  }
print "boardsize " . $size;
print "clear_board";
if ($h =~ s/\bKM\[([\d.+-]+)\]//gs)         {  print "komi $1";  }

my $handicap = 0;
my $set_free_handicap = "set_free_handicap";
if ($h =~ s/\bHA\[(\d+)\]//gs and $1 > 0) {  $handicap = $1;   }


# Handicap stones passed as setup stones ?
if ($handicap && $h =~ m/AB((\[\w\w\])+)/)
{
    my $stones = $1;   $stones =~ s/\[//g;
    foreach my $c (split(/\]/, $stones)) {
	$set_free_handicap .= " " . sgf2gtp($c);
	if (!--$handicap)  {  print "$set_free_handicap";  }
	if ($handicap < 0) {  die "shouldn't happen";  }
    }
}

sub play
{
    my ($color, $coord) = @_;
    if ($coord eq "") {  print "play $color pass"; return;  }

    my $c = sgf2gtp($coord);

    # Handicap stones passed as regular moves ?
    if ($handicap) {
	if ($color ne "B") { die "handicap stones are not black !\n"; }
	$set_free_handicap .= " $c";
	if (!--$handicap) { print "$set_free_handicap"; }
	return;
    }
    
    print "play $color $c";
}

my $movenum = 0;
my $last_color = 'w';

foreach (@m) {
    $maxmoves and $movenum >= $maxmoves and last;

    if (/\($/)                {  die "this script doesn't handle variations, aborting.\n"; }
    if (/(AB|AW|AE)\[\w\w\]/) {  die "this script doesn't handle setup stones, aborting.\n"; }

    if (/^([BW])\[()\]/ || /^([BW])\[(\w\w)\]/) {
	$last_color = $1;
	$movenum++;
	play($1,$2);
    }
}

if ($genmove) {
	print "genmove ".(uc $last_color eq 'W' ? 'B' : 'W');
}
