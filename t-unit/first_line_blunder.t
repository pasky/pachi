
% First line blunder (silly)
boardsize 7
. . . . . O .
. . O O O . .
. O X . X O .
. O X . X O .
O X X . X X O
X). . . . . X
. . . . . . .

first_line_blunder w a4 1
first_line_blunder w g4 0


% First line blunder (case 1)
boardsize 7
. . O . . . .
. . O . . . .
. . . X . . .
. O O X . . .
O X X X . . .
X). . . . . .
. . . . . . .

first_line_blunder w a4 1


% First line blunder (not case 1)
boardsize 9
. . . . . . . . .
. . . . . . . . .
. . O . . . . . .
. . O . . . O . .
. . . O . . . . .
. . . O . . . . .
. . O X X X O . .
. O O X . X O O .
X)X X X . X X X X

first_line_blunder w a2 0	# not case 1)
first_line_blunder w j2 1	# case 1)


% First line blunder (case 2)
boardsize 7
. . . . X . .
. . . . . . .
. . O O . X .
. . . . O X .
. . . . O O X
. . . . . . O)
. . . . . O .

first_line_blunder b g4 1

