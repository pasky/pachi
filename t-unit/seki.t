
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


% Simple seki (2 stones)
boardsize 6
. . O X . O
. O O X . O
. O X O O O
. O X X X X)
. O X . X .
. O X . X .

moggy status e6 :


% Simple seki (3 stones)
boardsize 6
. . O X . O
. . O X . O
. O O X O O
. O X O O X)
. O X X X X
. O X . X .

moggy status e6 :


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


# Not 3-stone seki (can connect out)
boardsize 7
. X). X X X .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . . O X
. . X X X O .
. . . . X X X

breaking_3_stone_seki w f5 0
breaking_3_stone_seki w g2 0
moggy status e4 X


# Not 3-stone seki (not fully surrounded)
boardsize 7
. X). X X X .
. X X X O O O
. . X . . . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X .

breaking_3_stone_seki w f5 0
breaking_3_stone_seki w f3 0


# Not 3-stone seki (dead shape)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O . X
. . X . O O X
. . X X X O O
. . . . X X).

breaking_3_stone_seki w f5 0
breaking_3_stone_seki w f4 0
moggy status e4 X


# Not 3-stone seki (bent, dead shape)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O X X
. . X . O . O
. . X X O O O
. . . X X X).

breaking_3_stone_seki w f3 0
breaking_3_stone_seki w f5 0
moggy status e4 X


% 3-stone seki (all connected, don't break)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

breaking_3_stone_seki w f5 1
breaking_3_stone_seki w f3 1
moggy status g6 O   f5 :   g4 X


% 3-stone seki (middle stone, don't break)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O X .
. . X . O X O
. . X . O X .
. . X X O O O
. . . X X X).

breaking_3_stone_seki w g5 1
breaking_3_stone_seki w g3 1
moggy status e4 O   g5 :   f4 X


# 3-stone seki (bent, don't break)
boardsize 7
. . X . X O O
. . X X X O .
. . X . O O X
. . X . O X X
. . X . O . O
. . X X O O O
. . . X X X).

breaking_3_stone_seki w g6 1
breaking_3_stone_seki w f3 1
moggy status e4 O   f3 :   g4 X


% 3-stone seki (ok to break)
boardsize 7
. X . X O . .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

breaking_3_stone_seki w f5 0
breaking_3_stone_seki w f3 0
moggy status g4 O


% 3-stone seki (must break !)
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

breaking_3_stone_seki w f6 0
breaking_3_stone_seki w f4 0
moggy status g4 O


% 3-stone seki (full board, don't break)
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

breaking_3_stone_seki b s11 1
breaking_3_stone_seki b s9  1
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
