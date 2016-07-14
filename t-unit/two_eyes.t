
% 2 eyes, one group
boardsize 5
. . . . .
X X . . .
. X . . .
X X . . .
. X . . .

two_eyes b1 1


% 2pt eye with prisoner
boardsize 5
. . . . .
X X X . .
X O X . .
X . X . .
. X X . .

two_eyes b1 1


% 2pt eye with prisoner
boardsize 5
. . . . .
X X X . .
X . X . .
X O X . .
. X X . .

!two_eyes b1 1		# FIXME


% Diag connection
boardsize 7
. . . . . . .
. . O O O O .
. O O X X O .
. O X . X O .
. . X X X O .
X X . O O . .
. X . . . . .

two_eyes b1 1


% Bamboo joint
boardsize 7
. . . . . . .
. . O O O O O
. O . . X X X
. O X . X O .
. . X . X X X
X X . O O O O
. X . O . O .

two_eyes b1 1


% Hanging connection
boardsize 7
. . . . . . .
. . O O O O O
. O . O X X X
. O X . X O .
. X . X X X X
X X . O O O O
. X . O . O .

two_eyes b1 1


% False eye
boardsize 5
. . . . .
O O O O O
O X X . O
X . X . O
. X O O .

two_eyes b1 0


% Open
boardsize 5
. . . . .
X X . . .
. X . . .
. X . . .
. X . . .

two_eyes b1 0


% Open
boardsize 5
. . . . .
. X . . .
X X . . .
. X . . .
. X . . .

two_eyes b1 0


% 2pt eye
boardsize 5
X X . . .
. X . . .
. X . . .
X X . . .
. X . . .

two_eyes b1 1


% 2pt eye
boardsize 5
X X . . .
. X . . .
O X . . .
X X . . .
. X . . .

two_eyes b1 1


% False eye
boardsize 5
X O O . .
. X O . .
X X . O .
. X . O .
. X . . .

two_eyes b1 0


% Ok
boardsize 5
X . X . .
. X . . .
X X . . .
. X . . .
. X . . .

two_eyes b1 1


% Ok too
boardsize 5
X . . . .
. X . . .
X X . . .
. X . . .
. X . . .

two_eyes b1 1


% Not ok
boardsize 5
X . O . .
. X . O .
X X . O .
. X . O .
. X . O .

two_eyes b1 0


% Big eye
boardsize 5
X X . X .
. X X X .
O X . . .
O X . . .
. X . . .

two_eyes b1 1


% No big eye area (yet)
boardsize 7
. . O O O O O
O O X X X X O
. X O X . X O
. X O . X X O
. X O . X . O
. O X X . O .
. O O O O . .

two_eyes c2 0
two_eyes c1 0		# 2pt eye not real yet


% Open
boardsize 5
X X . X .
. X X X .
O X . . .
O X . . .
. . . . .

two_eyes b2 0


% Big eye
boardsize 7
. . O O O O .
. . O X X . .
. . O X . X X
. . O X . X .
. . O X . X X
. . O X X . .
. . O O O O .

two_eyes d2 1




