
% 2 groups with one eye seki
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


##############################################################################
# Bad nakade moves

% Nakade move breaking seki
boardsize 7
. X O O O O O
. X X X O X X
. X . O O X .
. X . O X X X
O O O O X O .
. O O O X O O
O . O O X O).

moggy status f1 O        # Shouldn't nakade here !


% Nakade move breaking seki
boardsize 7
. X O O O . O
. X X X O O O
. X . O O X X
. X . O X X .
O O O O X X X
. O O O X . O
O . O O X O).

moggy status f1 O        # Shouldn't nakade here !


##############################################################################
# False eye seki


% False eye seki
boardsize 7
X . X O . . .
. X X O O . .
X X O . . O O
X O . O O X X
O . O X X X .
. O)O X O O X
. . O X . O .

moggy status f3 X        # Shouldn't fill false eye ...


##############################################################################
# Nakade seki: 3 stones

% Not 3-stone seki (can connect out)
boardsize 7
. X). X X X .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . . O X
. . X X X O .
. . . . X X X

breaking_nakade_seki w f5 0
breaking_nakade_seki w g2 0
moggy status e4 X

% Not 3-stone seki (can escape)
boardsize 7
. X). X X X .
. X X X O O .
. . X . . O X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X .

breaking_nakade_seki w g6 0
breaking_nakade_seki w f3 0


% Not 3-stone seki (can connect out)
boardsize 7
. X). X X X X
. X X X O O .
. . X . . O X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X .

breaking_nakade_seki w g6 0
breaking_nakade_seki w f3 0


% Not 3-stone seki (not fully surrounded)
boardsize 7
. X). X X X .
. X X X O O O
. . X . . . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X .

breaking_nakade_seki w f5 0
breaking_nakade_seki w f3 0


% Not 3-stone seki (dead shape)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O . X
. . X . O O X
. . X X X O O
. . . . X X).

breaking_nakade_seki w f5 0
breaking_nakade_seki w f4 0
moggy status e4 X


% Not 3-stone seki (bent, dead shape)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O X X
. . X . O . O
. . X X O O O
. . . X X X).

breaking_nakade_seki w f3 0
breaking_nakade_seki w f5 0
moggy status e4 X


% 3-stone seki (don't break)
boardsize 7
. X . X X X .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

breaking_nakade_seki w f5 1
breaking_nakade_seki w f3 1
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

breaking_nakade_seki w g5 1
breaking_nakade_seki w g3 1
moggy status e4 O   g5 :   f4 X


% 3-stone seki (bent, don't break)
boardsize 7
. . X . X O O
. . X X X O .
. . X . O O X
. . X . O X X
. . X . O . O
. . X X O O O
. . . X X X).

breaking_nakade_seki w g6 1
breaking_nakade_seki w f3 1
moggy status e4 O   f3 :   g4 X


% Not 3-stone seki (must break, extra eye)
boardsize 7
. X . X O . .
. X X X O O O
. . X . O . X
. . X . O O X
. . X . O . X
. . X X O O O
. . . X X X).

breaking_nakade_seki w f5 0
breaking_nakade_seki w f3 0
moggy status g4 O


% Not 3-stone seki (must break, false eye = 2nd eye)
boardsize 7
. . X X X X .
X X X O O O O
. X . O . X .
. X . O O X O
. X X . O X O
. . X . O O .
. . X X X O)O

breaking_nakade_seki w e5 0
breaking_nakade_seki w g5 0


% Not 3-stone seki (must break ! extra eye)
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

breaking_nakade_seki w f6 0
breaking_nakade_seki w f4 0
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

breaking_nakade_seki b s11 1
breaking_nakade_seki b s9  1
moggy status t12 X


##############################################################################
# Nakade seki: 4 stones

% Not 4-stone seki (dead shape)
boardsize 7
. . . X X X .
X X X X O O O
X . O O O . O
X . O . X X O
X . O O X X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w f5 0

% Not 4-stone seki (dead shape)
boardsize 7
. . . X X X .
X X X X O O O
X . O O . X O
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w e5 0
moggy status c4 X

% Not 4-stone seki (can become dead shape)
boardsize 7
. . X X X X .
X X X O O O O
X . . O X X .
X . . O . X O
X . . O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w e4 0
breaking_nakade_seki w g5 0
# status: undecided


% Not 4-stone seki (dead shape)
boardsize 7
. . . X X X .
X X X X O O O
X . O O O X O
X . O . X X .
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w g4 0
moggy status c4 X


% Not 4-stone seki (dead shape, adjacent libs)
boardsize 7
. . . X X X .
X X X X O O O
X . O O O X .
X . O O X X .
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w g4 0
breaking_nakade_seki w g5 0
moggy status c4 X


% Not 4-stone seki (can escape)
boardsize 7
. . . X X . .
X X X X O . O
X . O O O X O
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w f6 0


% Not 4-stone seki (can connect out)
boardsize 7
. . . X X X .
X X X X O . O
X . O O O X O
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w f6 0


% 4-stone seki (don't break)
boardsize 7
. . . X X X .
X X X X O O O
X . O O O X .
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 1
breaking_nakade_seki w g5 1
moggy status c4 O   e4 X


% 4-stone seki (don't break, middle stone)
boardsize 7
. . . X X X .
X X X X O O O
X . . O O X .
X . . O X X O
X . . O O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w g3 1
breaking_nakade_seki w g5 1
moggy status d4 O   e4 X


% Not 4-stone seki (live shape, must break)
boardsize 7
. . X X X X .
X X O O O O O
X . O . X X O
X . O O O X O
X . . . O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d5 0
breaking_nakade_seki w g3 0

% Not 4-stone seki (live shape, must break)
boardsize 7
. . . X X O O
X X X X O O X
X . . . O . X
X . X . O O X
X . . . O . X
X)X X X O O O
. . . X X X .

breaking_nakade_seki w f3 0
breaking_nakade_seki w f5 0

% Not 4-stone seki (live shape, must break)
boardsize 7
. . . X X O O
X X X X O O X
X . . . O . X
X . X . O . X
X . . . O O X
X)X X X O O O
. . . X X X .

breaking_nakade_seki w f4 0
breaking_nakade_seki w f5 0

% Not 4-stone seki (live shape, must break, extra eye)
boardsize 7
. . X X X O .
X X O O O O O
X . O . X X O
X . O O O X O
X . . . O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d5 0
breaking_nakade_seki w g3 0
moggy status c4 O   f4 O


% Not 4-stone seki (live shape, must break)
boardsize 7
. . X X O O O
X X X O O X O
X . . O . X O
X . . O O X O
X . . . O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w e5 0
breaking_nakade_seki w g3 0


% Not 4-stone seki (must break, extra eye)
boardsize 7
. . . X X O .
X X X X O O O
X . O O . X O
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w e5 0
moggy status c4 O   e4 O


% Not 4-stone seki (must break, false eye = 2nd eye)
boardsize 7
. X X X X X .
X X O O O O .
X . O . X O O
X . O O X X O
X . O . X O .
X)X O O O O O
. X X X X X .

breaking_nakade_seki w d3 0
breaking_nakade_seki w d5 0
moggy status c4 O   e4 O


% Not 4-stone seki (must break, false eye = 2nd eye)
boardsize 7
. X X X X X .
X X . . O O O
X . O O O X .
X . O X X X O
X . O O . O .
X)X . O O O O
. X X X X X .

breaking_nakade_seki w e3 0
breaking_nakade_seki w g5 0
moggy status c4 O   e4 O


% Not 4-stone seki (must break, false eyes = 2nd eye)
boardsize 7
. X X X X X .
X X O O O O O
X . O . X O .
X . O O X X O
X . O . X O .
X)X O O O O O
. X X X X X .

breaking_nakade_seki w d3 0
breaking_nakade_seki w d5 0
moggy status c4 O   e4 O


% 4-stones seki (full board)
boardsize 19
. . X . O . O X X X X . X . O . . . .
. X . X X O O O X O X . O X X O O . .
X . X O O O . O O O X . . O X X O . .
O X X X X X O O O X X X X . . X O . .
O O O O O O O X O O O O X X X . . . .
O X X X X X X X X O . X X O X O O O .
X X . . . X . X O O O O X O O X X O .
. . . X X O O X X X O O O O . X O . O
X . . X O . O X O X X O X O . X O O O
. X . X O . O O O X X X X O . X O X X
X . X X X O O O X X O X O O X)X X X .
X X . . X X X O . X O X . O X . O O O
. . . . . . O . O O O O O O X X X O X
. O . . . . . . . . O . X O X O X X X
. . . X X X . X . X O X . X O O X O X
. . O O O . . . . X X X . X X O X O .
. . . . . . O . . . . . O . . O O O .
. . . . . . . . . . . . . O . . . . .
. . . . . . . . . . . . . . . . . . .

breaking_nakade_seki b q8 1
breaking_nakade_seki b t9 1
moggy status p8 X   r8 O


##############################################################################
# Nakade seki: 5 stones

% Not 5-stone seki (dead shape)
boardsize 7
. . X X X X .
X X X O O O O
X . O O X X O
X . O . X X .
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w g4 0
moggy status c4 x


% Not 5-stone seki (dead shape)
boardsize 7
. . X X X X .
X X X O O O O
X . O O . X O
X . O . X X X
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w e5 0


% Not 5-stone seki (can escape)
boardsize 7
. . X X X . .
X X X O O . O
X . O O X X O
X . O . X X O
X . O O . X O
X)X X O O O O
. . X X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w e3 0


% Not 5-stone seki (can connect out)
boardsize 7
. . X X X X .
X X X O O . O
X . O O X X O
X . O . X X O
X . O O . X O
X)X X O O O O
. . X X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w e3 0


% 5-stone seki (don't break)
boardsize 7
. . X X X X .
X X X O O O O
X . O O X X .
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 1
breaking_nakade_seki w g5 1
moggy status c4 O   e4 X


% 5-stone seki (don't break, adjacent libs)
boardsize 7
. . X X X X .
X X X O O O O
X . O . X X O
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 1
breaking_nakade_seki w d5 1
moggy status c4 O   e4 X


% 5-stone seki (don't break, adjacent libs)
boardsize 7
. . X . O O O
X X X O . . O
X . . O X X O
X . . O X X O
X . . O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w e6 1
breaking_nakade_seki w f6 1
moggy status d4 O   e4 X


% Not 5-stone seki (dead shape, adjacent libs)
boardsize 7
. . X X X X .
X X X O O O O
X . . O X X O
X . . O X X .
X . . O O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w g3 0
breaking_nakade_seki w g4 0


% 5-stone seki (don't break, middle stone)
boardsize 7
. . X X X X .
X X X O O O O
X . . O X X .
X . . O X X O
X . . O O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w g3 1
breaking_nakade_seki w g5 1
moggy status d4 O   e4 X


% 5-stone seki (don't break) (empty triangle liberty)
boardsize 7
. . X X X X .
X X X O O O O
X . O O X X O
X . O . X X O
X . O O . X O
X)X X O O O O
. . X X X X .

breaking_nakade_seki w d4 1
breaking_nakade_seki w e3 1
moggy status c4 O   e4 X


% Not 5-stone seki (live shape, must break)
boardsize 7
. . X X X X .
X X X O O O O
X . . O X X O
X . . O . X .
X . . O O X X
X)X X X O O O
. . . X X X .

breaking_nakade_seki w e4 0
breaking_nakade_seki w g4 0
moggy status d4 O   f4 O


% Not 5-stone seki (live shape, must break, extra eye)
boardsize 7
. . X X X O .
X X X O O O O
X . . O X X O
X . . O . X .
X . . O O X X
X)X X X O O O
. . . X X X .

breaking_nakade_seki w e4 0
breaking_nakade_seki w g4 0
moggy status d4 O   f4 O


% Not 5-stone seki (must break, extra eye)
boardsize 7
. . X X X O .
X X X O O O O
X . O O X X .
X . O . X X O
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w g5 0
moggy status c4 O   e4 O


% Not 5-stone seki (must break, false eye = 2nd eye)
boardsize 7
. X X X X X .
X X O O O O O
X . O . X X .
X . O O X X O
X . . O X O .
X)X X O O O O
. . X X X X .

breaking_nakade_seki w d5 0
breaking_nakade_seki w g5 0
moggy status c4 O   e4 O


% 5-stone seki (full board)
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

breaking_nakade_seki b r10 1
breaking_nakade_seki b s11 1
moggy status t12 X


##############################################################################
# Nakade seki: 6 stones

% Not 6-stone seki (can escape)
boardsize 7
. X X X . . .
X X O O . O O
X . O X X . O
X . O X X X O
X . O O X O O
X)X X O O O O
. . X X X X .

breaking_nakade_seki w e6 0
breaking_nakade_seki w f5 0


% Not 6-stone seki (can connect out)
boardsize 7
. X X X X . .
X X O O . O O
X . O X X . O
X . O X X X O
X . O O X O O
X)X X O O O O
. . X X X X .

breaking_nakade_seki w e6 0
breaking_nakade_seki w f5 0


% 6-stone seki (don't break)
boardsize 7
. . X X X X .
X X X O O O O
X . . O X X .
X . . O X X X
X . . O O X .
X)X X X O O O
. . . X X X .

breaking_nakade_seki w g3 1
breaking_nakade_seki w g5 1
moggy status d4 O   e4 X


% 6-stone seki (don't break, adjacent libs)
boardsize 7
. X X X X X .
X X O O O O O
X . O . X X O
X . O . X X X
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 1
breaking_nakade_seki w d5 1
moggy status c4 O   e4 X


% Not 6-stone seki (must break, false eye = 2nd eye)
boardsize 7
. X X X X X .
X X O O O O O
X . O . X X .
X . O X X X O
X . O O X O .
X)X X O O O O
. . X X X X .

breaking_nakade_seki w d5 0
breaking_nakade_seki w g5 0
moggy status c4 O   e4 O


% Not 6-stone seki (must break, extra eye)
boardsize 7
. X X X X O .
X X O O O O O
X . O . X X O
X . O . X X X
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d4 0
breaking_nakade_seki w d5 0
moggy status c4 O   e4 O


% Not 6-stone seki (live shape, must break)
boardsize 7
. X X X X X .
X X O O O O O
X . O . . X O
X . O X X X X
X . O O O X O
X)X X X O O O
. . . X X X .

breaking_nakade_seki w d5 0
breaking_nakade_seki w e5 0

