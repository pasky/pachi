#!/bin/sh
size="$1"
opts="$2"
[ -n "$size" ] || size=9
[ -z "$opts" ] || opts=",$opts"

rm uctbook-$size-7.5.pachitree
n=0
genbook1()
{
	echo "[#$n:$1]"
	n=$((n+1))
	echo -e 'boardsize '$size'\nclear_board\nkomi 7.5\nuct_genbook b' | ./zzgo "games=1000000,explore_p=$1$opts"
}
genbook1 0.1
genbook1 0.2
genbook1 0.6
genbook1 1.0
genbook1 2.0
genbook1 0.6
genbook1 2.0
genbook1 0.2
genbook1 0.1
genbook1 0.1
