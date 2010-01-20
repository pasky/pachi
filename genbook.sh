#!/bin/sh
size="$1" # board size
opts="$2" # UCT engine options; must NOT specify different policy
popts="$3" # UCT policy options
[ -n "$size" ] || size=9
[ -z "$opts" ] || opts=",$opts"
[ -z "$popts" ] || popts=":$popts"

if [ "$size" -le 13 ]; then
	games=400000
else
	games=200000
fi

rm uctbook-$size-7.5.pachitree
n=0
genbook1()
{
	echo "[#$n:$1]"
	n=$((n+1))
	echo -e 'boardsize '$size'\nclear_board\nkomi 7.5\nuct_genbook b' |
		./zzgo -t =$games "policy=ucb1amaf:explore_p=$1$popts$opts"
}
genbook1 0.0
genbook1 0.1
genbook1 0.2
genbook1 2.0
genbook1 0.2
genbook1 2.0
genbook1 0.1
genbook1 0.0
genbook1 0.0
genbook1 0.0
