% Moggy testing


% Moggy moves: Sample moves in a given situation
boardsize 6
X X O X O O
X . O X . O
X . O X . O
. X X O O .
. . . . . .
. . . . . .

moggy moves (c3)		# Sample white moves after black c3


% Moggy status: Check outcome after some moggy games
boardsize 9
O . O . . O . . .
. O . O . O O X .
. O . . O O X X O
. . O O O X O O O
. X O X X X X X .
. X O O O X X X X
. X O X X X . . X
X X X O . . X X X
X O . . O . . . O

moggy status (c6) h8      # Black to play, last move = c6, check h8 status

moggy status (c6) h8 b5   # Black to play, last move = c6, check h8 and b5 status
moggy status (b)  h8 b5   # Black to play, random last move
moggy status      h8 b5   # Same
moggy status (w)  h8 b5   # White to play (random last move)


% Raw playout speed benchmark
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . X . . . . . . . . . . . . . . .
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

moggy status (d16) c17

