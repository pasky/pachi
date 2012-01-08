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

rm ucttbook-$size-7.5.pachitree
n=0
gentbook1()
{
	echo "[#$n:$1]"
	n=$((n+1))
	echo -e 'boardsize '$size'\nclear_board\nkomi 7.5\npachi-gentbook b' |
		./pachi -t =$games "policy=ucb1amaf:explore_p=$1$popts$opts"
}
gentbook1 0.0
gentbook1 0.1
gentbook1 0.2
gentbook1 2.0
gentbook1 0.2
gentbook1 2.0
gentbook1 0.1
gentbook1 0.0
gentbook1 0.0
gentbook1 0.0
