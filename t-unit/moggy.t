% Moggy testing


% Moggy moves: Sample moves in a given situation
boardsize 6
X X O X O O
X . O X . O
X . O X . O
. X X)O O .
. . . . . .
. . . . . .

moggy moves		# Sample white moves after black c3


% Moggy status: Check outcome after some moggy games
boardsize 9
O . O . . O . X .
. O . O . O O X .
. O . . O O X X O
. . O)O O X O O O
. X O X X X X X .
. X O O O X X X X
. X O X X X . . X
X X X O . . X X X
X O . . O . . . O

moggy status h8 ?     
moggy status h8 x   b5 X   a9 O		# Check h8, b5 and a9 status


% Two-headed dragon, false eyes should be b
boardsize 9
. . . . . . . . .
. O O O O O . . .
. O X X X O O O O
. O X . X X X X O
. O O X O O O X O
. . O X O . O X X
. . O X X O . O X
. . O O X O O O X
. . . O)X X X X .

moggy status d6 X


% Raw playout speed benchmark
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . X). . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .

moggy status c17 ?

