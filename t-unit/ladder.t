

% Basic ladder
boardsize 5
. . . . .
. . O . .
. O X . .
. . O O .
. . . . .

ladder b d3 1

% Blocked ladder
boardsize 7
. . . . . . .
. . . . . X .
. . . . . . .
. . O . . . .
. O X . . . .
. . O O . . .
. . . . . . .

ladder b d3 0


% Side ladder
boardsize 5
. . . . .
. O . . .
. X O . .
. O . . .
. . . . .

ladder b a3 1


% Side ladder
boardsize 4
. . . .
. O . .
. X O .
. O . .

!ladder b a2 1       # Currently fails, probably no big deal (false negative)



% False side ladder
boardsize 7
. . O . . . .
. . O X X X X
. O X O . . .
. O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder b a3 0        # Can be disastrous if this one fails (false positive !)
