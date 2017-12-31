

% Basic ladder
boardsize 5
. . . . .
. . O . .
. O X . .
. . O O .
. . . . .

ladder_any b d3 1

% Blocked ladder
boardsize 7
. . . . . . .
. . . . . X .
. . . . . . .
. . O . . . .
. O X . . . .
. . O O . . .
. . . . . . .

ladder_any b d3 0


% Side ladder
boardsize 5
. . . . .
. O . . .
. X O . .
. O . . .
. . . . .

ladder_any b a3 1


% Side ladder
boardsize 4
. . . .
. O . .
. X O .
. O . .

ladder_any b a2 1       # Currently fails, probably no big deal (false negative)


% False side ladder
boardsize 7
. . O . . . .
. . O X X X X
. O X O . . .
. O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder_any b a3 0        # Can be disastrous if this one fails (false positive !)

% False side ladder 2
boardsize 7
. . O . . . .
. . O X X X X
. O X O . . .
O O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder_any b a3 1


% Working side ladder with countercapture
boardsize 7
. . O O O . .
. . O X . O .
. O X O . O .
. O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder_any b a3 1        

% Long countercapture
boardsize 7
. . . . . . .
. . . . . . .
. O O . . . .
. O X . . . .
. X O . . . .
. O X X O . .
. . O O . . .

ladder_any b a3 1        

% Working side ladder with countercapture 2
boardsize 7
. . O O O . .
. . O X . O .
. O X O . O .
O O X X O . .
. X X O . . .
. O O . . . .
. . . . . . .

ladder_any b a3 1        # is_ladder() doesn't consider this a ladder ...


% Ladder with countercapture
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. X O . . . .
. O X . . . .
. . O O . . .
. . . . . . .

ladder_any b d3 0


% Middle ladder ends in suicide
boardsize 7
. . . . . . .
. . . . . . .
. . . . O . .
. . O . . O .
. O X . . . .
. . O O . . .
. . . . . . .

ladder_any b d3 1


% Working ladder with countercapture
boardsize 7
. . . . . . .
. . . . . . .
O . O O . . .
. X O X . . .
. O X X O . .
X X O O . . .
O O O . . . .

ladder_any b e4 1


% Working ladder with snapback
boardsize 7
. . . . . . .
. . . . . . .
O . O O . . .
O X O X . . .
. O X X O . .
X X O O . . .
O O O . . . .

ladder_any b e4 1

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

ladder_any b a5 1

% This one works (no countercapture actually)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X . . X O
. . O O . O .
. . . . . . .

ladder_any b d3 1   


% Works even though can countercap
boardsize 7
. . . . . . .
. . . . . O .
. . . . . . .
. . O . . . .
. O X . . X .
. . O O . O .
. . . . . . .

ladder_any b d3 1

% Can escape (countercap)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X . . X .
. . O O . O .
. . . . . . .

ladder_any b d3 0

% Can escape (countercap)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . . O . . .
. . O X . . .
. . . O O X .
. . . . . . .
ladder_any b e3 0

% Can countercap (but not right away)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . O . . . .
. O X X O . .
. . O O X . .
. . . . O . .
ladder_any b d4 0

% Unusual start
boardsize 7
. . . . . . .
. . . . . . .
. . . . . O .
. . O O O X X
. O X . X X O
. . O O . O .
. . . . . . .

ladder_any b d3 1	# is_ladder() doesn't handle these either


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
ladder_any b e3 0


% Triple ko, no ladder
boardsize 7
. . . O . . .
O O O X O O O
O X . . X X X
. O . X X . X
O X X X O X O
O O O . O O O
. . . . . . .

ladder_any b c5 0
