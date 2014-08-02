% Basic self-atari check
boardsize 3
XX.
XXX
XX.
sar b c1 1
sar b c3 1
sar w c1 1
sar w c3 1

% Basic suicide check
boardsize 3
XXX
XXX
XX.
sar b c1 1
sar w c1 0

% Almost-nakade
boardsize 3
OOO
..X
XX.
sar b c1 0
sar w c1 1
sar b b2 0
sar w b2 1
sar b a2 0
sar w a2 1

% Bulky-five / tetris-four nakade
boardsize 3
OOO
..X
XXX
sar b b2 0
sar w b2 0
sar b a2 1
sar w a2 1

% Rabbity six nakade 1
boardsize 4
XXOO
.X.O
OXOO
OOOO
sar w a3 1
sar b a3 0
sar w c3 1
sar b c3 1

% Rabbity six nakade 2 - seki
boardsize 4
X.OO
.XXO
OXOO
OOOO
sar w a3 1
sar b a3 1
sar w b4 1
sar b b4 1

% Rabbity six nakade 3
boardsize 4
XXOO
XX.O
O.OO
OOOO
sar w b2 1
sar b b2 0
sar w c3 1
sar b c3 0

% Weird three nakade
boardsize 4
XXX.
XOOO
X.XX
XXXX
sar w b2 1
sar b b2 1
sar w d4 1
sar b d4 1

% Bulky five nakade
boardsize 5
XXXXX
XOOXX
XO.XX
XX.XX
XXXXX
sar b c3 0
sar w c3 0
sar b c2 1
sar w c2 1

% Capture-from-within 2pt-eye nakade
boardsize 3
OOO
O..
OOO
sar w b2 1
sar b b2 0
sar w c2 1
sar b c2 0

% Capture-from-within 3pt-eye (straight) nakade
boardsize 3
OOO
.X.
OOO
sar w a2 1
sar b a2 0
sar w c2 1
sar b c2 0

% Capture-from-within 3pt-eye (bent) nakade
boardsize 3
O.O
OX.
OOO
sar w b3 1
sar b b3 0
sar w c2 1
sar b c2 0

% Capture-from-within 4pt-eye (square) nakade
boardsize 3
O.X
O.X
OOO
sar w b3 1
sar b b3 0
sar w b2 1
sar b b2 0

% Eye falsification nakade
boardsize 3
OOO
..X
OOO
sar w a2 1
sar b a2 1
sar w b2 0
sar b b2 0

% Bulky five multi-w-group nakade
boardsize 5
XXXXX
XOOXX
XO..X
XXOXX
XXXXX
sar b c3 0
sar w c3 1 # technically, this is 0, but since black is in atari, we misevaluate (it is ok!)
sar b d3 1
sar w d3 0 # captures!

% Multi-b-group nakade
boardsize 5
.XX..
XOOX.
XO.XX
XX.XX
..XXX
sar b c3 0
sar w c3 1
sar b c2 0
sar w c2 1
sar b d5 0
sar w d5 0 # throw-in
sar b e4 0
sar w e4 1
sar b e5 0
sar w e5 0

% Real multi-b-group nakade
boardsize 6
XXXOO.
XO.XO.
X.OXO.
OXXXO.
OOXXO.
OOOOO.
sar b b4 1
sar w b4 0
sar b c5 1
sar w c5 0

% Almost multi-b-group nakade
boardsize 6
XXXOO.
XO.XO.
X.OXO.
OXXXO.
O.XXO.
OOOOO.
sar b b4 0
sar w b4 1
sar b c5 0
sar w c5 1

% Almost multi-b-group nakade (mirrored)
boardsize 6
.OOOOO
.OXX.O
.OXXXO
.OXO.X
.OX.OX
.OOXXX
sar b d2 0
sar w d2 1
sar b e3 0
sar w e3 1

% Eyeshape-avoidance nakade 1
boardsize 4
XXXX
XO.X
XX.X
XXXX
sar w c3 0
sar w c2 1

% Eyeshape-avoidance nakade 2
boardsize 4
XXXX
XO.O
XX.X
XXXX
sar w c3 0
sar w c2 1

% False nakade
boardsize 5
X.XX.
XOOXX
XOOX.
X.OXX
XOOXX
sar w b2 1
sar b b2 1
sar w b5 1
sar b b5 0

% Not-quite-snapback
boardsize 5
XXXXO
XXX.O
XXX.O
OOOXX
OO...
sar b b1 0
sar w b1 1
sar b c3 0
sar w c3 1
sar b c4 1
sar w c4 1

% Snapback
boardsize 4
XXO.
..X.
OX..
....
sar b a1 1
sar w a1 0
sar b a3 1
sar w a3 0
sar b b3 0
sar w b3 0
sar b d4 0
sar w d4 1

% Real game 1
boardsize 9
O.O..OXX.
.O.O.OOX.
OO..OOXXO
XOOOOXOOO
XXOXXXXXO
.XOOOXXXX
.XOXXX..X
XXXO..XXX
XO..O...O
sar w j8 1
sar b j8 0
