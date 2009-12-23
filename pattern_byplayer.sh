#!/bin/sh
# pattern_byplayer - Build a per-player database of used patterns
#
# Invoke this script for each played game, it will add patterns
# to the database incrementally; each file in the database contains
# patterns played by one player.
#
# You must already have a spatial dictionary generated.

dbdir="$1"; shift
sgf="$1"; shift
# rest of parameters is passed to the patternscan engine

sgf_attr() {
	# GNU sed mishandles CRLF lines
	cat "$sgf" | tr -d '\r' | sed -n -e 's/'$1'\[\([^]]*\)\]/\1/p'
}

black="$(sgf_attr PB)"
white="$(sgf_attr PW)"
handi="$(sgf_attr HA)"

if [ -n "$handi" ] && [ "$handi" -gt 0 ]; then
	to_play=white
	# Comment following out if you want to include handi games.
	echo "$sgf: Skipping handicap game" >&2
	exit 1
fi

to_play=black
./sgf2gtp.pl "$sgf" | ./zzgo -e patternscan "$@" |
	sed -n -e 's/^= //p' | grep -v '^ *$' | # skip irrelevant replies
	while read pattern; do
		if [ "$to_play" = black ]; then
			player="$black"
			to_play=white
		else
			player="$white"
			to_play=black
		fi
		echo "$pattern" >>"$dbdir/$player"
	done
