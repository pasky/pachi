

% Basic ladder
boardsize 5
. . . . .
. . O . .
. O X . .
. . O O .
. . . . .

ladder b c3 1

% Blocked ladder
boardsize 7
. . . . . . .
. . . . . X .
. . . . . . .
. . O . . . .
. O X . . . .
. . O O . . .
. . . . . . .

ladder b c3 0


% Side ladder
boardsize 5
. . . . .
. O . . .
. X O . .
. O . . .
. . . . .

ladder b b3 1


% Side ladder
boardsize 4
. . . .
. O . .
. X O .
. O . .

ladder b b2 1       # Currently fails, probably no big deal (false negative)


% False side ladder
boardsize 7
. . O . . . .
. . O X X X X
. O X O . . .
. O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder b b3 0        # Can be disastrous if this one fails (false positive !)


% False side ladder 2
boardsize 7
. . O . . . .
. . O X X X X
. O X O . . .
O O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder b b3 0


% Working side ladder with countercapture
boardsize 7
. . O O O . .
. . O X . O .
. O X O . O .
. O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder b b3 1


% Long countercapture
boardsize 7
. . . . . . .
. . . . . . .
. O O . . . .
. O X . . . .
. X O . . . .
. O X X O . .
. . O O . . .

ladder b b3 1

% Working side ladder with countercapture 2
boardsize 7
. . O O O . .
. . O X . O .
. O X O . O .
O O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

!ladder b b3 1        # don't consider this a ladder right now ...


% Ladder with countercapture
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. X O . . . .
. O X . . . .
. . O O . . .
. . . . . . .

ladder b c3 0


% Middle ladder ends in suicide
boardsize 7
. . . . . . .
. . . . . . .
. . . . O . .
. . O . . O .
. O X . . . .
. . O O . . .
. . . . . . .

ladder b c3 1


% Working ladder with countercapture
boardsize 7
. . . . . . .
. . . . . . .
O . O O . . .
. X O X . . .
. O X X O . .
X X O O . . .
O O O . . . .

ladder b d4 1


% Working ladder with snapback
boardsize 7
. . . . . . .
. . . . . . .
O . O O . . .
O X O X . . .
. O X X O . .
X X O O . . .
O O O . . . .

ladder b d4 1

% Side ladder works
boardsize 9
. X X . . . . . .
. O X . . . X . .
. O X X . . . . .
. O O X . . X X X
. X O X X X . . .
. O X X O O O O O
. O X O O . . . .
. X X O . . O . .
. . . O . . . . .

ladder b b5 1

% This one works (no countercapture actually)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X . . X O
. . O O . O .
. . . . . . .

ladder b c3 1


% Works even though can countercap
boardsize 7
. . . . . . .
. . . . . O .
. . . . . . .
. . O . . . .
. O X . . X .
. . O O . O .
. . . . . . .

ladder b c3 1

% Can escape (countercap)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X . . X .
. . O O . O .
. . . . . . .

ladder b c3 0

% Can escape (countercap)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . . O . . .
. . O X . . .
. . . O O X .
. . . . . . .
ladder b d3 0

% Can countercap (but not right away)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X X O . .
. . O O X . .
. . . . O . .
ladder b d3 0

% Unusual start
boardsize 7
. . . . . . .
. . . . . . .
. . . . . O .
. . O O O X X
. O X . X X O
. . O O . O .
. . . . . . .

!ladder b c3 1   # TODO Support this kind of ladder as well ?


% ko, no ladder
boardsize 7
. . . . . . .
. . . . . . .
. . X O . . .
. O X O . . .
O . O X . . .
. O X O O . .
. . X . . . .

ko c3
ladder b d3 0


% Triple ko, no ladder
boardsize 7
. . . O . . .
O O O X O O O
O X . . X X X
. O . X X . X
O X X X O X O
O O O . O O O
. . . . . . .

ladder b b5 0


% Crazy ladder, 120 moves.
boardsize 19
. . . . . . . . . . . . . X . . . . .
. . . . . . . . . . . . O X . . X . .
. . O . . . . . . . . . O O X X . X .
. X . O . O . . . . . . . . O O O . .
. . . . . . . . . . . . . . . . . . .
. . X . X . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . O . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
X . . . . . . . . . . . . . . . . . .
X O O O . . . . . . . . . . . . X . .
. O X . . . . . . . . . . . . . . . .
. . . X . . . . . . . . . . O . X . .
. . . . . . . X . . O . . . O X . . .
X X X . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . O)X

ladder b t1 1


% Another crazy one, 150 moves.
boardsize 19
. . . . . . . . O O O O O O O . . . .
. . . . . . . . . . . X X . . X . . .
. . . . . . . . . . . O . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . X .
. . . . . . . . . . . . . . . O O . .
. . . . . . . . . . . . . O O . . . .
. . . . . . . . . . . . . . . X . . .
. . . . . . . . . . . . . . . . X . .
. . . . . . . . . . . . . . . . X . .
. . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .
O X . . . . . . . . . . . . . . X . .
O X O O O O . . . . . . . . . . . . .
O X . . . . . . . . . . . . . . . . .
. O O . . . . . . . . . . . . . . . .
. . . X . X . . . . . . . . . . . O)X

ladder b t1 1


% Nakayama ladder problem
% https://senseis.xmp.net/?NakayamaLadderProblem
boardsize 19
. . . . O . . . . . . . . . . . . . .
O . . . . . . . . . . . . . . . . . O
. . . . . . . . . . . . . . . . . . O
. . . O O X O . . . . . . . . . . . .
O . . . O X O O O O . O . . . . . O .
O X X X X X X X X X O . . . . . . . .
X O O O . X . . . . X . . . . . . . O
X . O O O X . . . . X O . . . . . . .
X O O O O X . . . . X . . . . . . . .
O X X X X X X X X X O . . . . . . . O
O O . . . X O . . O . . . . . . . . .
. O . O . X . O . . . . . . . . . . .
. . . . . . O . . . . . . . . . . . .
. . . . O . . . . X . . . . . . . . .
O . . . . X . . . X . . . X O . . . .
. . . . . X . . . X . . . X . . . . .
. . . . . X . . . X . . . X . . . . .
. . . . O . X X X X X X X O . . . O .
. O . . . X)O O O O O O O . O . . . .

ladder_any w n1 1


% Treasure chest enigma (Nakayama Noriyuki).  Run with -d9 !
boardsize 13
. . . X X . . . X X . . .
. . . O O . X . O O X . .
. . . . . . . O . O . . .
X . . . . . O . X . . . X
X O . . . X O X . . . O X
X O . . . . . . . . . O X
X . . . . . . . . . . . X
. . . . . . . . . . . . .
. . . . . . . . . . . . .
. . . . . . X). . . . . .
. . . . . X O . . . . . .
. . . . . X O X . . . . .
. . . . . . X . . . . . .

ladder w g3 1
