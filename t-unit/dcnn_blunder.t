
#############################################################
# Test regular blunders

% First line connect blunder
# see also first_line_blunder.t
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . X . . . X X O . . O O X X X . . .
. X . . X . X O . O O X X . . X . . .
. O O O X . . O . O X X . X X X . . .
. . . X O O . . . O X O X X . . X . .
. O . X . . . O . O O O . . . O O . .
. O X . . . X . . . . O . . . . . . .
. . O X X . . . X . X X X . . . . . .
. . . O X . X . . . . . . . . . . . .
. . O O O O . . . . . . . . . . O . .
. . . X . . . O . . . . . . . . . . .
. O O X . . . . . . . . . . . . . . .
O X X . . . . X X . . . . . . O . . .
X). . . X . . . . . . . . . X X O . .
. . . . . O . X . O . . . . . . O . .
. . . X . . . . . . . O O O X X X . .
. . . X . O . X X O O X X X . . . . .
. . . X O . . O O O . O X . . . . . .
. . . . . . . . . . O . . . . . . . .

dcnn_blunder w A8


% First line connect blunder 2
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . X . . . X X O . . O O X X X . . .
. X . . X . X O . O O X X . . X . . .
. O O O X . . O . O X X . X X X . . .
. . . X O O . . . O X O X X . . X . .
. O . X . . . O . O O O . . . O O . .
. O X . . . X . . . . O . . . . . . .
. . O X X . . . X . X X X . . . . . .
. . . O X . X . . . . . . . . . . . .
. . O O O O . . . . . . . . . . O . .
. . O X . . . O . . . . . . . . O . .
O O O X . . . . . . . . . . . . O . .
O X X . . . . X X . . . . . . . . . .
X . . . X . . . . . . . . . X X . O .
. X . . . O . X . O . . . . . . X O .
. . . X . . . . . . . O O O X X X X O
. . . X . O . X X O O X X X . . . . X)
. . . X O . . O O O . O X . . . . . .
. . . . . . . . . . O . . . . . . . .

dcnn_blunder w T5


% First line connect blunder 3
boardsize 19
. . O)X . . . . . . . X X O X X . X .
. . . O X . X . . . . X O O O X X X O
. . . O X X O X X . X X X O O X O O O
. O O O O O O O . O X X O O O O O O O
O . O X . . . . . . . X O O X X O X .
. O X X . O X X X X X . O X O X X X .
. X . X . X X O O O X O O X . . O . .
. . . O X X O . . X O X O X O X X . .
. . . O O O . O . . O X . X X . . . .
. . . . . . . . . . O . . X O . X X .
. O O O . . . . O . . . . X . O X O .
. . X O X . O . . . . . O X X O O . O
. . X X O O O X X . . X X . . X O O .
. . . . X X O O X . . X O X X X X O .
. . . X . . O . X . . X O O X . . O .
. . X . . X O . X O O O . O X . O O O
. . X O . . O . X O . X . O . X X O .
. X O . O . . . X O . O X O X . . X O
. . X . . . . . X X O . . O . . . X .

dcnn_blunder b E19


% 4-4 reduce 3-3 blunder
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . X)O O . . . . . . . . . O O X . .
. . O X X O . . . . . . . O X X . X .
. . X X . O . . . O . . . O . X X . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . X . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . X . . . . . . X . . . . . . X . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. O . X . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . O .
. . . . . . . . . . . . . . . . . . .
. . X X . O . . . . . . . O . X X . .
. . O X X O . . . O . . . O X X O . .
. . X O O . . . . . . . . . O O X . .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder w B17 B3 !S3		# S3 works


% group 2lib blunder
boardsize 19
komi 6.5
. O X X . . . . . . . . . . . . . . .
. O O X . X X . . . . . . . . . . . .
. . O O X X O . X X X . . X . . X X .
. . . O X O O O O O O X X . . X X O X
. O O . X X X X O X O O O X . . X O .
O . O X . . . O O X O . X O X X O . O
. O X X O O O O X X O . . O O O O O .
X O X . X X X O)X O . . . . . O . . .
. X . X . . . . X O . . . . . . . . .
. . . . . . . . X O . . . . . . O . .
. . X . . . X X O O . . O O O O X O .
. . . . . X O O . O . O X X O X X O .
. X X . . X O . O . . O X . X . X O .
. O O X X . X O . . . O X X X O X . O
O O . O . . . . O O . . O O X . X . O
O X . O . . . X O . O . . O X . . X O
X . X O . X X . X O O O . O O X . X X
. X O O X . . X . X O X O O X X . . .
. X . . . . . . . X X X X O . . . . .

dcnn_blunder b H11


#############################################################
# Test boosted moves

% Atari ladder_big defense
boardsize 19
. . . . . . . O). . . . . . . . . . .
. . . . X . X O O O . . . . . . . . .
. . X X O X X O X . . . . . . . . . .
. . X O O O O X X . O . . X . X . . .
. . . X O . X . . . . . . . . . . . .
. . . X O . . . . . . . . . . . O . .
. . . X O . . . . . . . . . . . . . .
. . . . X O . . . . . . . . . . . . .
. . . . X . . . . . . . . . . . . . .
. . . X . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . O . .
. . . . . . . . . . . . . . . . . . .
. . O . . . . . . . . . . . . O . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder boosted b  F18 D18 D19 C18  !G19


% Check ladder breakers don't all get boosted (drops 31)
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . X X . . . . . . . . . . . . .
. . X X O O O . . . . . . X . . . . .
. . O O . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . O . X . . O X). . . . . . . . . .
. . O . . O . . X . . . . . . . . . .
. . . X X X O O X . . . . . . . . . .
. . O X . O X X O O . . . . . . . . .
. . O X . . . . X O . . . . . X . . .
. . O O X X . . . . . . . . . . . . .
. . . O O X . . . . . . . . . . . . .
. . X O X . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . O . O . . . . . . . . . . . . .
. . . . . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder boosted w  G12 G13 H12   !L16


% Check boosted moves really saves group
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . O . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . O O O .
. . . . . . . . . . . . . . X O X X .
. . . . . . . . . . . . . . X X O . .
. . O . . . . . . . . . . . X O O . .
. . . . . . . . . . . . . . . X X). .
. . . O . . . . . X . . . . . . . O .
. . . . . X . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder boosted w  S7 T8 S6 T7  !S5	# S5 doesn't save group, only defends S7


% Test best combo filter
boardsize 19
. . . . . . . . . . . . . . . . . O .
. . . . . X . . . . . . . O . . . . O
. X X X . X O . . . . X X O . O O O X
. X O O O X . . . . . . O X X X X X X
. O O . . O O . . X . X X O . . . . .
. X X O . . . . O O X . O O X . . . .
. . . . . . . . . . O . O X X . . . .
. . O . . . . . . . . O X O . . . . .
. . . O O X . . . O O X X . . . . . .
. . X X X O O . . . X O . . . X . . .
. . . O X X O O O O O X X . . . . . .
. X . O X X X X X X O O . X . X . . .
. O . O . O . . . . X O . . . . . . .
. . . . . O O X . . X O . . . X . . .
. . . O . X O X . . X O . O . . . . .
. . . . . O X X . . . X O . . X . . .
. . . O O . O . . . . X X O O O)X . .
. . . . O O X X . . . . . X . O O X .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder boosted b  R4 S4 S3   !S1 !R5 !Q5 !P5 !T1 !T2 !P4 !T3
# S1 R5 Q5 etc only fix one atari


% Test best combo filter + big ladder (drops 72)
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . X . . . O . X O . .
. . . X . . . . . . . . . . . X O . .
. . . . . . . . . . . . . . . X)X O .
. . . . . . . . . . . . . . X X O . .
. . . . . . . . . . . . . . . O . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . X . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder boosted w  S13 R13 S14  !R12 !T14


% Test snapback, selfatari
boardsize 19
. . . . . . . . . . . . . . . . . O .
. . . . . . . . . . . . . . . O O X X
. . . . . O . . . . . . . O . . X O .
. . . X . . . . . O . . . O X X . O X
. . . . . . . . O . . . X)X O X . O .
. . . . . . . X X O O . . O O X X X X
. . . X . . . . . X X O O O X X O O .
. . . . . . . . . . . X X X O O . . .
. . . . . . . . . . . . . . X O . . .
. . . O X . . . . X . . . . X O . . .
. . . O X . . . . . . . . . X O . . .
. . . O X . . . . . . . . . X O . . .
. . . O X . . . . . . . . . X O . . .
. . O X . . . . . . . . . . . X O . .
. . O X . . . . . . . . X . . X O . .
. . O X . . X . . . . . . . . X O . .
. . O X . . . . . X . . . . . X O . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

dcnn_blunder boosted w  M14 !M15 !N14		# M15 creates snapback,  N14 selfatari


% Test atari_and_cap silly moves
boardsize 19
. O . . . . . . . . . . . . . . . O .
X X O O . . . . . . O O . . O O O X X
. . X O . O . . O . X X O O X . X O O
. X . X O . O O . O . . X O X X . O X
. . . X O O X X O . X). X X O X . O .
. . . X O X . X X O O O . O O X X X X
X X . X X X . . . X X O O O X X O O X
X O X X . . . . . . . X X X O O . O X
O O O X . . . . . . . . . . X O . O X
. O . O X . . . . X . . . . X O O X .
. . . O X . . . . . . . . . X O X . .
. . . O X . . . . . . . . . X O X X O
. . . O X . . . . . . . . . X O O O .
. . O X . . . . . . . . . . . X O . .
. . O X . . . . . . . . X . . X O . .
. . O X . . X . . . . . . . . X O . .
. . O X . . . . . X . . . . . X O . .
. O O O X X . . . . . . . X X O O . .
. . . . . . . . . . . . . . . . . . .

# fixme currently we boost [ K15 M15 L16 N14 J16 ]
#   K15: ok   M15: ok  L16: ok
#   N14: really bad, turns into double atari
#   J16: bad but hard to check (3libs)

!dcnn_blunder boosted w    K15 M15 L16  !N14

