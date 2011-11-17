#!/bin/sh

if [ $# -ne 3 ]; then
	echo "Usage: $0 PLAYER1 PLAYER2 BOOKDIR" >&2
	exit 1
fi

export CMDDIR=$(pwd)
export SEQDIR=$(mktemp -d)

if [ ! -d "$3" ]; then
	mkdir "$3"
	{ echo 8; echo 16; } >"$3"/stats
fi

while true; do
	(cd "$3"; "$CMDDIR"/walk.sh "$1" "$2")
	sleep 5
done
