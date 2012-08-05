#!/bin/sh
# Simple script using Pachi for rating all moves in a given situation.
# Pachi will analyse each followup move and suggest a winrate for it;
# higher is better.
#
# Usage: tools/sgf-ratemove.sh SGF MOVENUM PACHIARGS...
#
# Note that this script assumes dynkomi=none and does not show
# dynamic komi information. (Would be trivial to add but it's tricky
# to interpret the data.)
#
# This script is dirty and insecure for untrusted input!
#
# Example: tools/sgf-ratemove.sh progame.sgf 120 -t =500 -d 0
# ...to get 500 simulations per each possible move in programe.sgf
# at move 120.
# 
# If you want to know more details on what is Pachi thinking about the
# various moves, remove the `-d 0` part. To improve the accuracy of values
# (at the expense of run time), raise the value of 500 (try 2000 or 10000;
# 100000 will usually already take couple of hours). The values will be
# most useful in the middle game; in fuseki and most yose situations,
# expect a lot of noise.

sgf=$1; shift
movenum=$1; shift
tools/sgf2gtp.pl -g -n $movenum <"$sgf" |
	sed -e 's/genmove/0 pachi-evaluate/' |
	./pachi "$@" |
	sed -ne '/^=0/,${s/^=0 //;p}' |
	sort -r -n -t ' ' -k 2
