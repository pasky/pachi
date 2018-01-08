% Some life & death situations that are a bit of a gray area for moggy.
% Can be useful as regression test when making policy changes to check
% if l&d is getting better or worse.


% Test 1 - Dead but potential for screwup on the right
boardsize 19
. . O . X X). . . . O X . . . . . . .
. O X . X O O O O O O O X X . . . . .
. . O X X X O X X . X O O O X X . . .
. . O O X X O O X X X O X X O . X X .
. . . O O X X X . . X X X . X . . O .
. . O X O O O X X O O O O . O O O . .
. . O X . . . O X . . . . . . X . . .
. O X . X O . . . X X O O X . . . . .
. O X X X X X . . X . X X O O O O . .
. . O O O O . X X O X . O O . X . . .
. . . . . X X O O O O X X X X . X O .
. . O X . X O O X X O X . X O . . O .
. . . . X O O . O X X O O X O O O . .
. . X X O . . . O O X X X . O X O . .
. . X O O O O . X O O O X X O X X X .
. . X O O X . . O X X X O X X X . . .
. . X O O X . O . . O O O . . . . . .
. . X X O X . . . . . . . O . X . . .
. . . . X . . . . . . . . . . . . . .

sar b n19 1
moggy status j18 x		# origin: j18 black [56-70]%


% Test 2 - Dead side group
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . X . X . . . . . . . . . . . . . .
. . O X . X . . . . . . . . . X . . .
. . O O . . . . . X . . X . . . X . .
. . . . . . . . . . . . . . . . . . .
. . O . . . . . . . . . . . O O O . .
. . . . . . . . . . . . . . . X . . .
. . . X . . . . . . . . . . . . . . .
. . . X O O . . . . . . . . O O O . .
. . X O . X O O . . . . . . . X . . .
. . X O . X O . O O . . . O O . X . .
. X)O X X . X O X . . O O X O . . . .
. . O O O X X . X O O X X X X O O O .
. O . . O X O X X X O O O X X X O X .
. X O O O X O O X X X O O O X X X O .
X . X O X X X O O O X X O O X X . X .
. X X O O X . O . . O O X X . . O . .
. . X X O X . O X O O X . X X O . . .
. . . . X . . . . . . . X . . . . . .

moggy status d7	x	# origin: d7 black 65%


% Test 3 - Can live
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . X . X . . . . . . . . . . . . . .
. . O X . X . . . . . . . . . X . . .
. . O O . . . . . X . . X . . . X . .
. . . . . . . . . . . . . . . . . . .
. . O . . . . . . . . . . . O O O . .
. . . . . . . . . . . . . . . X . . .
. . . X . . . . . . . . . . . . . . .
. . . X O O . . . . . . . . O O O . .
. . X O . X O O . . . . . . . X . . .
. . X O . X O . O O . . . O O . X . .
. X O X X . X O X . . O O X O . . . .
. . O O O X X . X O O X X X X O O O .
. O . . O X O X X X O O O X X X O X .
. X O O O X O O X X X O O O X X X O .
X . X O X X X O O O X X O O X X . X .
. X X O O X . O . . O O X X . . O . .
. . X X O X . O X O O X . X X O . . .
. . . . X . . . . . . . X). . . . . .

moggy status h2	o	# origin: h2 white 82%


% Test 4 - Endgame misevaluation
boardsize 19
. X . X . . . . . . . . . . X O O O .
O O X X X X O X X . O O O X . X O . O
. X O X O X . . . . O)X . . X X X O O
. O O O O X . . . X . . X X O . X X O
. O X X O X . . . . . X O O O . . O .
. . O X X X . X . . . . X X O O O . .
. . O O O X X O X X X . X O O X . . .
. . . O X X O O O X . X . X X O . . .
. . O . O X X O . O X O . X O O O O .
O . O O . X O . . O . O . X X X O X .
. O O X X X O O . . O O O O O X X O .
O X O X O O O . O O O . O X O X O O .
. X O O X X O O . O . O X X X O O . .
X X X X O X . . O O X X X . O X O O O
. . X O O X . X O X X . . . . X X X O
. . X O . O . O X X O X . . . X . . X
. . X O O X . O X O O X X . . . . X .
. . X X O O . O O O O O X . . . . . .
. . X X X X O . O X X X X X . . . . .

moggy status m18 x		# origin: m18 black 84%,  Winrate  [ black 54% ]


% Test 5 - Dead group
boardsize 19
. X . X . X . O . X). . . . . . . . .
. O X X X O O . O O X X . . . . . . .
O . O X . X X O O X . X . X X X . X .
. O O O X X . X O X X O X O O . X X O
O O X . X O X X X O X O X O . . . O .
O X . X X O O . X . . . X . O O O . .
X . X X O X O X . X . . . . O X . O .
. O . O O X O O X . . X X X X O . . .
. . . O X . . O X O . X O O O O O O .
. O O X . X O . X O X X X X X X X X X
. O X X O X O X . O O O O O O X X O .
O X O O . . O X . . . . . X O X O O .
. X X O . . . O . . . . O X X O O O .
. . X O . . . . . . . . O X . X O X .
. X O O . . . . . . . . O O X X X . X
. . X O . . . . . . . O X X . X . X .
. . X O O X . O . . O . O X . . . . .
. . X X O O . . . . . . O X X . . . .
. . . X X O . . . . . . O O X . . . .

moggy status j18 X		# origin: j18 black 91%, Winrate  [ black 68% ]


% Test 6 - This one shouldn't live
boardsize 19
. X . X . . . . O X X . . X O O X O .
X O X . X . X X O . O O O O X . X X X
. O O X . X . O O O O X O X X X X O O
. . O O O X . O X X O X X X O . X X O
. X . O X X X O O X X O X O O X X O O
X O O O X . . X X . X O X O O O O O .
X)O . O . . . . . X . O O X . X O . .
. X O O . X X O X O . O X X . X . O .
X . X X X O X X O . O . X O O O O . .
. X . X O . O O . X O . X O X X . . .
. . . X O O O . O O O O X X O O X O .
. O O X O . X X X X O X X . O X X . .
. . X O O X O O O X X . . . O O X O .
. X X O . . O . X O X X . . O X O O .
. X O O . O X . . O O X . X O X X X .
. . X O . O X O . . O X . . X X . . .
. . X O O X O O . . O . X . . . . . .
. . X X O X O . . . X X . . . . . . .
. . X X X X O . . . . . . . . . . . .

moggy status b17 x		# origin: b17 black 64%


% Test 7 - Corner semeai (b wins)
boardsize 19
. X . X . . . . . . . . . . . . X . .
. O X . X . . . . . . X O O O X . X .
O . O X . X . . . X X X X X O X X O .
. O O O X . . . . X O O X O X X X O .
X O X X . X X . . . X O X O X X O O .
. X O . X O O . X . O O X O O O O . .
. X . O O X O . . O O X X X . X . . .
. X O O X X O X O O . O X . . . . . .
. X . O O X X X X X O O . . O O O . .
. O O . X . . X O O X . . . . X . . .
. . . . . . X O . O . . . . O . X . .
. . O O X . . O O O . O O . . O O X .
. O X X . X . O X . . O . O X O X . .
. O X O X . X X O O O X O X . X . X .
. O O O . . . X X X X X . . X X X . .
. . X O . X . O O . . . . . . X . . .
. . X O O X . O X O O X X . . . . . .
. . X X O O . . . . . O X). . . . . .
. . . . . . . . . . . . . . . . . . .

moggy status b16 x		# origin: b16 black 59%


% Test 8 - Simple atari defense check
boardsize 19
. . . . O X X X X O . O X X . . . . .
O O O . O O X O O . . O O X . . . . .
O X X O . O X X O O O . O X X . . . .
X X . X O O X X X O X . O O X . X . .
. . X X O O O O X X O O O O O X . . .
. . . X X O X X X O O X O O X X . X .
X X X O O O O X O O X X O O O X . O O)
O O X O O . O X O X X X O X O X X O X
O . O . . O X X O X O X O X X X . X .
. O O O O X X O O O O X X X O O X X X
. . O X X X X O O O O O O O O O X O O
. . X O X . X X X O X O X O X O O . O
. . . O X X . . X O X X X X X O . O .
. . . O O X O . X O O O O X X X O . .
. O X O . O X X X O X O X X X O . O .
O . O O . O O X . X X X X O O O . . .
O O X X O O X X . X X . X X O . O . .
O X X . X O X X X . . . X O O . O . .
X X . X X O O O O X . . X X O . . O .

moggy status t13 x		# origin: t13 black 94%
