
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
moggy status d9 O   e9 :   f9 X 


% Nakade move breaking seki  FIXME
boardsize 7
. X O O O O O
. X X X O X X
. X . O O X .
. X . O X X X
O O O O X O .
. O O O X O O
O . O O X O).

moggy status f1 O        # Shouldn't nakade here !


% Nakade move breaking seki  FIXME
boardsize 7
. X O O O . O
. X X X O O O
. X . O O X X
. X . O X X .
O O O O X X X
. O O O X . O
O . O O X O).

moggy status f1 O        # Shouldn't nakade here !


% False eye seki  FIXME
boardsize 7
X . X O . . .
. X X O O . .
X X O . . O O
X O . O O X X
O . O X X X .
. O)O X O O X
. . O X . O .

moggy status f3 X        # Shouldn't fill false eye ...


% Don't break seki
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

moggy status g6 O   f5 :   g4 X


% Ok to break seki
boardsize 7
. X . X O . .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

moggy status g4 O


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

moggy status g4 O


% Early seki (3-stones)
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

moggy status t12 X

% Early seki (5-stones)
boardsize 19
. . . . . . . . . . . . . . . . . . .
. . . . . . X O O O O . O O X . O . .
. . . . . . X X X X X O O X . X O . .
. . . X . . . . . . O X O X . X . O .
. . X . . . . . . X . X X O X . . . .
. . . . . . . . . X . X O O O O O . O
. . X . . . X . . . O O X O O . . O O
. . X O X . O X X . . O X X O . X X X
. . O X X . X . . X O O X O O X X . O
. . O O . . O . X . O X X X . X . O O
. . . . . O . . . O O X X X X X X O O
. . O . . . O O O O X O O O X X X X X
. X O . . . . X X X X . . O O O O O .
. X O . O . . X O O O O O X . O X . .
. X O . . X . . X X X X X X O X . . .
. X O X . . . . . . . . . O O X O O .
. . X . . X . . . . . X X O X O X . .
. . . . . . . . . . . . . X X O O). .
. . . . . . . . . . . . . . . . . . .

!moggy status t12 X           # FIXME
