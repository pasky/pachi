#!/usr/bin/perl -n
#
# pattern_getdrops - Find significant value drops in kgslog GTP stream
# and convert them to lead-in game records for pattern learning
#
# Run as pipeline:
# 	tools/kgslog2gtp.pl | tools/pattern_getdrops.pl | tools/pattern_bayes_gen.sh -

BEGIN {
	use vars qw(@game $lastval $valthres $valfrac);
	$valthres = 0.8;
	$valfrac = 0.95;
}

chomp;
@_ = split(/\s+/);

if ($_[0] eq 'pachi-mygame') {
	@game = @_[1..4];
	print STDERR "*** new game [@game]\n";
	$lastval = 0;
	@commands = ();

} elsif ($_[0] eq 'pachi-mymove') {
	if ($lastval > 0 and $lastval < $valthres and $_[2] / $lastval < $valfrac) {
		print STDERR "large value drop $lastval -> $_[2] [@game :: @_]\n";
		print "$_\n" for @commands;
		print "play $game[0] $_[1] 1\n";
	}
	$lastval = $_[2];
	my $cmd = "play $game[0] $_[1] 0 $_[2]";
	push @commands, $cmd;

} elsif ($_[0] eq 'play') {
	push @_, 0;
	push @commands, "@_";

} else {
	push @commands, "@_";
}
