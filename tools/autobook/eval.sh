#!/bin/sh
#
# Choose next move to choose in the current directory (node)

# Config:
# Exploration coefficient
explore_p=0.8

best_move=
best_val=-9999

{ read mwins; read msims; } <stats

for move in $(find . -maxdepth 1 -type d | tail -n +2); do
	{ read wins; read sims; } <"$move"/stats
	val="$(echo "($wins/$sims+$explore_p*sqrt(l($msims)/$sims))*1000" | bc -l)"
	val="${val%.*}"
	if [ "$val" -gt "$best_val" ]; then
		best_val="$val"
		best_move="$move"
	fi
done

echo ${best_move#./}
