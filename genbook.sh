#!/bin/sh
rm uctbook-9-7.5.pachitree
genbook1()
{
	echo -e 'boardsize 9\nclear_board\nkomi 7.5\nuct_genbook b' | ./zzgo games=1000000,policy=ucb1amaf:explore_p=$1
}
genbook1 0.2
genbook1 0.6
genbook1 1.0
genbook1 2.0
genbook1 2.0
genbook1 0.6
genbook1 2.0
genbook1 0.2
genbook1 0.2
