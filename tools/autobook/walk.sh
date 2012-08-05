#!/bin/sh
#
# A single iteration of the algorithm

# Config:
# After how many visits (including prior_sims!) a node is expanded.
expand_sims=26
# Virtual loss
vloss=4
# Twogtp path
twogtp_path=/home/pasky/gogui-1.3.2/bin/gogui-twogtp

# Expected env:
# $CMDDIR
# $SEQDIR

pachi="$1"; shift
opponent="$1"

seq=""
color=W
while true; do
	{ read wins; read sims; } <stats
	xsims=$((sims+vloss))
	{ echo $wins; echo $xsims; } >stats.new
	mv stats.new stats # atomic

	children=$(find . -maxdepth 1 -type d | wc -l);
	case $children in
		1) expanded=;;
		*) expanded=1;;
	esac

	if [ -z "$expanded" -a "$sims" -ge "$expand_sims" ]; then
		echo "(;FF[4]GM[1]CA[UTF-8]RU[Chinese]SZ[9]HA[0]KM[7.5]PW[white]PB[black]$seq)" >"$SEQDIR/a.sgf"
		"$CMDDIR"/expand.sh "$pachi"
		expanded=1
	fi
	if [ -z "$expanded" ]; then
		break;
	fi

	move=$("$CMDDIR"/eval.sh)

	cd "$move"
	case $color in
		B) color=W;;
		W) color=B;;
	esac

	sgfmove=$(echo "$move" | perl -nle 'my ($x,$y) = /(.)(.)/; $x=lc($x); $x=chr(ord($x)-1) if ord(lc $x) > ord("i"); $y = chr(96+10-$y); print "$x$y"')
	seq="$seq;${color}[$sgfmove]"
done

echo "   *** Sequence: $seq"
echo "(;FF[4]GM[1]CA[UTF-8]RU[Chinese]SZ[9]HA[0]KM[7.5]PW[white]PB[black]$seq)" >"$SEQDIR/a.sgf"
rm -f "$SEQDIR"/r*

# last move has been... - we want to simulate this being _our_ move yet,
# i.e. start the simulation with the opponent to play
case $color in
	B)
		black="$pachi"
		white="$opponent";;
	W)
		black="$opponent"
		white="$pachi";;
esac
$twogtp_path -black "$black" -white "$white" -auto -verbose -size 9 -komi 7.5 -sgffile "$SEQDIR/r" -games 1 -openings "$SEQDIR"
wincolor=$(cat "$SEQDIR"/r-0.sgf | sed -ne 's/.*RE\[\(.\).*/\1/p')
case $wincolor in
	B) result=1;;
	W) result=0;;
esac
echo "   *** Result: $wincolor wins => $result"
pwd; ls

while [ -e stats ]; do
	case $color in
		B) nresult=$result;;
		W) nresult=$((1-result));
	esac
	echo "    + Recording $nresult .. $(pwd) color $color"

	{ read wins; read sims; } <stats
	wins=$((${wins%.*}+nresult))
	sims=$((sims-vloss+1))
	{ echo $wins; echo $sims; } >stats.new
	mv stats.new stats # atomic

	cd ..
	case $color in
		B) color=W;;
		W) color=B;;
	esac
done
