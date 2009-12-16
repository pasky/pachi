#!/bin/sh
# pattern_mm - Harvest patterns from game collection and compute feature strengths
#
# This is a "frontend" for the MM tool by Remi Coulom, that is assumed to be
# unpacked and compiled in the mm/ subdirectory - get it at
#
#	http://remi.coulom.free.fr/Amsterdam2007/mm.tar.bz2
#
# It will scan a given SGF collection, collect patterns, and use the MM tool
# to compute the relative strength of various features. The output will be
#
#	* patterns.gamma: Gamma values of pattern features
#	* patterns.gammaf: Gamma values of pattern features for MC simulations
#
# If you haven't done so yet, you should first run ./pattern_spatial_gen.sh
# (probably in the competition scan mode) to initialize the spatial patterns
# dictionary for the collection.
#
# If you run this on hundreds of games, be sure you are doing it on local
# filesystem, with some free memory (and few GB of free disk on both
# local fs and in /tmp for temporary data), and armed by a lot of patience
# - it can take long time (minutes, tens of minutes...).


if [ -z "$mm_file" ]; then
	mm_file=patterns.gamma mm_par= ./pattern_mm.sh "$@"
	mm_file=patterns.gammaf mm_par=,matchfast ./pattern_mm.sh "$@"
	exit
fi

echo "Gathering patterns..."
(for i in "$@"; do ./sgf2gtp.pl $i; done) |
	./zzgo -e patternscan competition$mm_par |
	sed -ne 's/^= //p' | grep -v '^$' |
	./pattern_enumerate.pl >/tmp/patterns.enum
ls -l /tmp/patterns.enum

# There must not be pipeline here, because of aux patterns.fdict file!

echo "Invoking MM..."
cat /tmp/patterns.enum | ./pattern_mminput.pl | mm/mm >/tmp/patterns.mm

echo "Associating gamma values..."
cat /tmp/patterns.mm | sed 's/  */ /; s/^ //;' | join -o 2.3,1.2 /tmp/patterns.mm patterns.fdict | sed 's/^s\.[0-9]*:/s:/' >$mm_file

rm -f /tmp/patterns.enum /tmp/patterns.mm
echo "Product:"
ls -l $mm_file
echo "Leaving behind for analysis:"
ls -l patterns.fdict mm-with-freq.dat
