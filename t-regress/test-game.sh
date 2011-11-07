#!/bin/sh
#
# test-game: Run testcases within a single game
#
# Usage: test-game FILENAME
#
# Examine a game record and run testcases for all moves described
# in the "game comment".

sgf="$1"

sed -ne '/GC\[/,/\]/{ s/.*GC\[//; s/\].*//; p; }' <"$sgf" |
	while read move class desc; do
		moven="${move%:*}"
		movec="${move#*:}"
		echo "Move $moven ($movec)"
		tools/sgf2gtp.pl -g -n $((moven-1)) <"$sgf" | ./pachi -d 4 -t =20000
		echo "$class: $movec"
	done
