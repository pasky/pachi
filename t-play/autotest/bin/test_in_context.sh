#!/bin/sh

# $1: path to test results
# $2: board size

(cd "$1" && ls f$2-*summ*dat) | sed 's/^f'$2'-//; s/[-.].*$//;' | xargs git log --pretty=oneline --no-walk | while read c s; do
	echo
	echo ================================================
	git log -1 --pretty=medium $c
	for t in $(cd "$1" && ls f$2-${c:0:5}*summ*); do
		echo
		echo $t
		cat "$1/$t" | cut -f 1,2,7,8,12,13
	done
done
