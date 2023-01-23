
% Test bent-four (all 4 corners dead)
boardsize 9
X . O X O . X X X
X O O X O O O O .
X . O X . . . O O
O O O X X . X . .
X)X X . X X X X X
. . X X . X O O O
O O . X X X O . X
. O O O O X O O X
X X X . O X O . X

moggy status  B2 x   B8 x   H2 x   H8 x


% Test bent-four (seki, all corners alive)
boardsize 9
X O . X O . X X X
X O O X O O . O O
X . O X . O O O O
. O O X X . X X X
O)O X . X X X O O
X X X X . X O O .
. O O O X X O . X
O O . O O X O O X
X X X . O X . O X

moggy status  B2 o   B8 o   H2 o   H8 o      A1 x   A9 x  J1 x   J9 x


% Test bent-four (bent-three, all corners dead)
boardsize 9
X X . O X O . X X
X O O O X O O O X
. O . X X . . O .
O O . X X . X O O
X)X X . X X X X X
O O X X . X . O O
. O . X X X . O .
X O O O X O O O X
X X . O X O . X X

moggy status  B2 x   B8 x   H2 x   H8 x


% Bent-three but not bent-four (seki)
boardsize 9
. . X O O X . O O
O). X O . X X X O
. . X O O O O O .
. . X X X X X O O
X X . . . . . X X
O O X X X X X . . 
. O O O O O X . .
O X X X . O X . .
O O . X O O X . .

moggy status  C3 o  B2 x   G7 o  H8 x   


% Two full-board bent-fours (dead)
boardsize 19
. . . X X O . . . . . . . . . X O . X
. X . X O . O . X . . . . . . X O O X
. . . X X O . O X . . . . . . X O O X
. . . X O O . O . X . X . X . X O O .
. . . . X . X O . . . O X . . X X O O
. . . X X . X O X X . X . . O X X X X
. . . X O O O O X . X O O . O O . O .
. X X O . O . O O X O . . . . . O . .
X . . . O . . . . O O . . . . . . . .
O X X X X X . X O X . . O . . O . . .
O O . . . . . . . . O O X O O . . . .
. O O . O . O O O O X O X X X O . . .
. X . . . O X X O X X X X O O O O . .
. X O . . O O X X . . . . . X X X O .
X O O . O X X X X X . . X X . . O . .
. X O O O O X . O . X X O . X . O O .
X X X X X X O O . O O O O X . X X O .
. X . X O O . . O . O O X X . . . X O
O O O X . . O . . X O X X . . . . X).

moggy status  A3 o    R19 x
