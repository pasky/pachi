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
#	* patterns.spat: Dictionary of spatial feature positions
#	* patterns.gamma: Gamma values of pattern features
#
# If you run this on hundreds of games, be sure you are doing it on local
# filesystem, with some free memory (and few GB of free disk for temporary data),
# and armed by a lot of patience - it can take long time (minutes, tens...).


echo "Gathering patterns..."
# This is a poor man's UNIX pipe from pattern_gather to pattern_enumerate.
rm -f patterns
mkfifo patterns
(cat patterns | sed -ne 's/^= //p' | grep -v '^$' | ./pattern_enumerate.pl >/tmp/patterns.enum) &
PATARGS=",competition" ./pattern_gather.sh "$@"
wait
rm -f patterns
ls -l /tmp/patterns.enum

# There must not be pipeline here, because of aux patterns.fdict file!

echo "Invoking MM..."
cat /tmp/patterns.enum | ./pattern_mminput.pl | mm/mm >/tmp/patterns.mm

echo "Associating gamma values..."
cat /tmp/patterns.mm | sed 's/  */ /; s/^ //;' | join -o 2.3,1.2 /tmp/patterns.mm patterns.fdict | sed 's/^s\.[0-9]*:/s:/' >patterns.gamma

rm -f /tmp/patterns.enum /tmp/patterns.mm
echo "Product:"
ls -l patterns.gamma patterns.spat
echo "Leaving behind for analysis:"
ls -l patterns.fdict mm-with-freq.dat
