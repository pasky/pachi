
% Ladder
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X X . . .
. . O O . . .
. . . . . . .

wouldbe_ladder w e3 1
wouldbe_ladder w d4 0


% Blocked ladder
boardsize 7
. . . . . . .
. . . . . X .
. . . . . . .
. . O . . . .
. O X X . . .
. . O O . . .
. . . . . . .

wouldbe_ladder w e3 0


% Trivial ladder
boardsize 7
. . . . . . .
. . . . . . .
. . . O . . .
. . O . . . .
. O X X . . .
. . O O . . .
. . . . . . .

wouldbe_ladder w e3 0    # Ok, wouldbe_ladder() looks for non-trivial ladders only atm ...


% Unusual ladder (non adjacent libs)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O O O . .
. O X X X . .
. . O . O O .
. . . . . . .

wouldbe_ladder w d2 1

