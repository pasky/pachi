#!/bin/bash

# Config:
# Minimal number of simulations for the result to be considered trustworthy
min_sims=50

if [ $# -ne 1 ]; then
	echo "Usage: $0 BOOKDIR" >&2
	exit 1
fi

export CMDDIR=$(pwd)

scan() {
	seq="$1"
	for i in */; do echo $(sed -ne 2p $i/stats) $i; done | sort -rn |
		{ first=1
			while read sims move; do
				move="${move%/}"
				if [ "$sims" -lt "$min_sims" ]; then
					break;
				fi
				[ -z "$first" ] || echo "$seq | $move"
				first=
				(cd "$move"; scan "$seq $move")
			done
		}
}

cd "$1"
scan ""
