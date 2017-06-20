
% Seki !
boardsize 9
O O O O . X X X O
. O O O X X X O O
O O O X X X O O O
X X X X O O . O O
. X X O O . O O .
X X X O O O . O O
X X O O . O O O O
X O O O O O . O O
O)O . O O . O . O

moggy moves
moggy status a1


% Early seki
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . . . X O O O O . O O X . O . .
. . . . . . X X X X X O O X . X O . .
. . . X . . . . . . O X O X . X . O .
. . X . . . . . . X . X X O X . . . .
. . . . . . . . . X . X O O O O O . O
. . X . . . X . . . O O X O O . . O O
. . X O X . O X X . . O X X O X X X X
. . O X X . X . . X O O X O O O X . O
. . O O . . O . X . O X X X . O X X O
. . . . . O . . . O O X X X X O X . O
. . O . . . O O O O X O O O X X X X X
. X O . . . . X X X X . . O O O O O .
. X O . O . . X O O O O O X . O X . .
. X O . . X . . X X X X X X O X . . .
. X O X . . . . . . . . . O O X O O .
. . X . . X . . . . . X X O X O X . .
. . . . . . . . . . . . . X X O O). .
. . . . . . . . . . . . . . . . . . .

moggy status t11


% Nakade move breaking seki
boardsize 7
. X O O O O O
. X X X O X X
. X . O O X .
. X . O X X X
O O O O X O .
. O O O X O O
O . O O X O).

!moggy status d1         # Eeek, shouldn't nakade here !


% Ok to break seki
boardsize 7
. X . X O . .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

moggy status g4


% Must break seki !
boardsize 9
. . . . . . . . .
. X . X X X X X .
. . X . O O O X .
. . X . O . X O O
. . X . O O X O .
. . X . O . X O O
. . X . O O O X).
. . . X X X X X .
. . . . . . . . .

moggy status g4
