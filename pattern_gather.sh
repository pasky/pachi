#!/bin/sh
# pattern_gather: Gather patterns from a SGF collection
#
# We will first gather all spatial features from the SGF collection
# (we take files as arguments), keep only 5000 most frequently occuring
# in the dictionary, and then do full pattern-matching again with this
# dictionary.
#
# DO NOT RUN THIS CONCURRENTLY!
# You really want to run this on a fast filesystem, not NFS or anything.
# During the conversion, you will need about 100M per ~100 games, after
# it's over it will take much less.
#
# To get patterns in competion mode (also get information about unplayed
# patterns present at the board, not just the single played one per move),
# run this script as:
#
#	PATARGS="competition" ./pattern_gather.sh ...
#
# Similarly, you can set SPATIALS to different number than 5000 to consider
# more spatial patterns.

[ -n "$SPATIALS" ] || SPATIALS=5000

rm -f patterns.spat

echo "Gathering population of $SPATIALS most popular spatials..."
(for i in "$@"; do echo $i >&2; ./sgf2gtp.pl $i; done) |
	./zzgo -e patternscan gen_spat_dict${PATARGS:+,$PATARGS} |
	sed 's/ /\n/g' | sed -ne 's/)//; s/^s:/0x/p; ' | # pick out spatial payloads
	perl -nle 'print (((1<<24)-1) & hex $_)' | # convert to ids
	sort -n | uniq -c | sort -rn | # sort by frequency
	head -n $SPATIALS | awk '{print$2}' | # take N top ids
	cat >/tmp/pattern.pop

echo "Composing new patterns.spat..."
# Preserve top comments
sed -e '/^[^#]/Q' patterns.spat >/tmp/patterns.spat
# join needs lexicographic order
sort /tmp/pattern.pop >/tmp/pattern.filter
grep -v '^#' patterns.spat | sort | join - /tmp/pattern.filter | # patterns with id in pattern.filter
	sort -n | cut -d ' ' -f 2- | perl -pe '$_="$. $_"' | # re-number patterns
	cat >>/tmp/patterns.spat

echo -n "Counting hash collisions... "
perl -lne 'chomp; my ($id, $d, $p, @h) = split(/ /, $_); foreach (@h) { next if $h{$_} = $id; print "collision $id - $h{$_} ($_)" if $h{$_}; $h{$_}=$id; }' /tmp/patterns.spat | wc -l

echo "Deploying patterns.spat in final position..."
mv /tmp/patterns.spat patterns.spat
rm /tmp/patterns /tmp/pattern.pop /tmp/pattern.filter


# Now, re-scan patterns with limited dictionary!
echo "Gathering patterns (2nd pass)..."
(for i in "$@"; do ./sgf2gtp.pl $i; done) |
	./zzgo -e patternscan >patterns

echo "Gathered pattern data in .:"
ls -l patterns patterns.spat
