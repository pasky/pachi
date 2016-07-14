% Useful side ladder
boardsize 9
. X X . . . . . .
. O X . . . X . .
. O X X . . . . .
. O O X . . X X X
. X O X X X . . .
. O O X O O O O O
. O X X O . . . .
. X X O O . O O .
. . . O . . . . .

useful_ladder b a5 1	# Only chance to win this game


% Real game
boardsize 19
. . . . . . . . . . . . . . . . . . .
O X O . . O . . . . . . . . . . . . .
. O X X X O . X . . O . . O . . . . .
. . . X O O O X X X O . . . . X . . .
. X X O . O X X O O X . . . . . . . .
. . O . O O X O X O X . . . . . X . .
. . . O X X O O . O . . . . . . . . .
. . . . X O . . . . . . . . . . . . .
. . . X . X X O . . . . . . . . . . .
. . . X X O X O . O . . . . . X . . .
. . . . . . O X . . . . . . . . . . .
. . . . . . O X . . . . . . . . . . .
. . O . . X X X . O . . . . . . . . .
. X X O O O . . . . O . . . . X . . .
. . . X . . . X X X X O . . O X . . .
. . . X O O O X O . X O . O . X . . .
. . X O X X X O O O X X X O . . . . .
. . X O O O X O . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

useful_ladder b b19 1
useful_ladder b g1 0	# Group still safe


% Long ladder ...
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X . . . .
. . O O . . .
. . . . . . .

useful_ladder b d3 0


% Group not surrounded
boardsize 9
. . . . . . . . .
. O . . . . X . .
. O X X . . . . .
. O O X . . X X X
. X O X X X . . .
. O O X O O O O O
. O X X O . . . .
. X X O O . O O .
. . . O . . . . .

useful_ladder b a5 0


% Group still safe after escape + capture
boardsize 9
O O O . O X . . .
O . . O O X . . .
X . . O X . . . .
O O O X X . . . .
X X X X X . . . .
. . . . . X X X X
O O O O O O O O O
O . O . . . . . O
. O O . . . . . X

useful_ladder b b7 0



% Life & death
boardsize 9
O O O . X . . . .
O . . O X . . . .
X . . O X . . . .
O O O . X . . . .
X X X X X . . . .
. . . . . X X X X
O O O O O O O O O
O . O . . . . . O
. O O . . . . . X

!useful_ladder b b7 1	# FIXME  Only move to win this game !
useful_ladder b h1 0	# 2 eyes already




