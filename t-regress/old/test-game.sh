#!/bin/sh
#
# test-game: Run testcases within a single game
#
# Usage: test-game.sh FILENAME
#
# Examine a game record and run testcases for all moves described
# in the "game comment".
#
# Pass any extra Pachi parameters in PACHIARGS. E.g.
#	PACHIARGS='-t =100000 -d 2 threads=2' testgame.sh ...

sgf="$1"

sed -ne '/GC\[[0-9][0-9]* .*\]/{s/.*GC\[\([^]]*\)\].*/\1/p; q;}; /GC\[/,/\]/{ s/.*GC\[//; s/\].*//; /^[0-9][0-9]* /p; }' <"$sgf" |
	while read moven cases desc; do
		echo "Examining move $moven"; sleep 1
		tools/sgf2gtp.pl -g -n $((moven-1)) <"$sgf" | ./pachi -t =20000 $PACHIARGS
		echo "Testcases: $cases ($desc)"
		echo "Confirm and press enter..."; read xx </dev/tty
	done
