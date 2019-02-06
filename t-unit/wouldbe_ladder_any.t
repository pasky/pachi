
% Ladder
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X X . . .
. . O O . . .
. . . . . . .

wouldbe_ladder_any w e3 1
wouldbe_ladder_any w d4 0


% Blocked ladder
boardsize 7
. . . . . . .
. . . . . X .
. . . . . . .
. . O . . . .
. O X X . . .
. . O O . . .
. . . . . . .

wouldbe_ladder_any w e3 0


% Trivial ladder
boardsize 7
. . . . . . .
. . . . . . .
. . . O . . .
. . O . . . .
. O X X . . .
. . O O . . .
. . . . . . .

wouldbe_ladder_any w e3 1    # unlike wouldbe_ladder() ...


% Unusual ladder (non adjacent libs)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O O O . .
. O X X X . .
. . O . O O .
. . . . . . .

wouldbe_ladder_any w d2 1

% Snapback
boardsize 7
. . . . . . .
. . . . . . .
. . X O . . .
. X . . O . .
. O X X X O .
. O O O O O .
. . . . . . .

wouldbe_ladder_any w c4 1
