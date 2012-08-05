#!/usr/bin/perl -ln
#
# kgslog2gtp - Convert kgsGtp log file to GTP command stream
#
# The command stream contains two special GTP commands intended
# for further script processing (unrecognized by Pachi itself):
#
#	pachi-mygame COLOR OPPONENT DATE TIME
#	pachi-mymove COORD VALUE
#
# (You probably want to convert pachi-mymove to play.)

chomp;

# 19.1.2012 20:47:47 com.gokgs.client.gtp.a <init>
if (s/^(\S+ \S+) com.gokgs.client.gtp.a <init>$/$1/) {
	$a = $1;

# FINE: Starting game as white against Mateman
} elsif (s/^FINE:.*as (\S+) against (\w+)/$1 $2/) {
	print "pachi-mygame $_ $a";

# IN: play b o3
} elsif (s/^IN: // and not /genmove/ and not /kgs-chat/ and not /time/) {
	print $_;

# *** WINNER is C4 (3,4) with score 0.4333 (49243/93153:93153/93393 games), extra komi 0.000000
} elsif (s/^\*\*\* WINNER is (\S+) .*score (\S+) .*komi 0\.0.*/$1 $2/) {
	print "pachi-mymove $_";
}
