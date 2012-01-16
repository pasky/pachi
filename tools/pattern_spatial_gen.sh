#!/bin/sh
# pattern_spatial_gen: Initialize spatial dictionary from a SGF collection
#
# We will first gather all spatial features from the SGF collection
# (we take files as arguments) and store these occuring more than 4 times
# in a freshly created spatial dictionary; afterwards, you will probably want
# to do standard pattern-matching.
#
# DO NOT RUN THIS CONCURRENTLY! The dictionary will get corrupted.
#
# To get spatials in competion mode (also get information about unplayed
# spatials present at the board, not just the single played one per move),
# run this script as:
#
#	PATARGS="competition" ./pattern_spatial_gen.sh ...
#
# Similarly, you can set SPATMIN to different number than 4 to include
# spatial features with other number of occurences.

[ -n "$SPATMIN" ] || SPATMIN=4

rm -f patterns.spat

echo " Gathering population of spatials occuring more than $SPATMIN times..."
(for i in "$@"; do echo $i >&2; tools/sgf2gtp.pl <$i; done) |
	./pachi -d 0 -e patternscan gen_spat_dict,no_pattern_match,spat_threshold=$SPATMIN${PATARGS:+,$PATARGS} >/dev/null

echo " Renumbering patterns.spat..."
perl -i -pe '/^#/ and next; s/^\d+/++$a/e' patterns.spat

echo -n " Counting hash collisions... "
perl -lne 'chomp; my ($id, $d, $p, @h) = split(/ /, $_); foreach (@h) { next if $h{$_} = $id; print "collision $id - $h{$_} ($_)" if $h{$_}; $h{$_}=$id; }' patterns.spat | wc -l
