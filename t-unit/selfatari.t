% Basic self-atari check
boardsize 3
X X .
X X X
X X .

bad_selfatari b c1 1
bad_selfatari b c3 1
bad_selfatari w c1 1
bad_selfatari w c3 1

% Basic suicide check
boardsize 3
X X X
X X X
X X .

bad_selfatari b c1 1
bad_selfatari w c1 0

% 2 stones suicide check
boardsize 3
X X .
O . X
X X X

bad_selfatari w b2 1
bad_selfatari b b2 0


##############################################################################
# Nakade

% Capture-from-within 2pt-eye nakade
boardsize 3
O O O
O . .
O O O

bad_selfatari w b2 1
bad_selfatari b b2 0
bad_selfatari w c2 1
bad_selfatari b c2 0

% Not Capture-from-within 2pt-eye nakade
boardsize 4
. . . .
. O O O
. O . .
. O O O

bad_selfatari b c2 1
bad_selfatari b d2 1

% Almost-nakade
boardsize 3
O O O
. . X
X X .

bad_selfatari b c1 0
bad_selfatari w c1 1
bad_selfatari b b2 0
bad_selfatari w b2 1
bad_selfatari b a2 0
bad_selfatari w a2 1

% Bulky-five / tetris-four nakade
boardsize 3
O O O
. . X
X X X

bad_selfatari b b2 0
bad_selfatari w b2 0
bad_selfatari b a2 1
bad_selfatari w a2 1

% Bulky-five
boardsize 3
O O O
. X .
X X X

bad_selfatari b c2 0
bad_selfatari w c2 1  # Makes bent 4, bad normally    (except in corner ... FIXME ?)
bad_selfatari b a2 0
bad_selfatari w a2 1

% Rabbity six nakade 1
boardsize 4
X X O O
. X . O
O X O O
O O O O

bad_selfatari w a3 1
bad_selfatari b a3 0
bad_selfatari w c3 1
bad_selfatari b c3 1

% Rabbity six nakade 2 - seki
boardsize 4
X . O O
. X X O
O X O O
O O O O

bad_selfatari w a3 1
bad_selfatari b a3 1
bad_selfatari w b4 1
bad_selfatari b b4 1

% Rabbity six nakade 3
boardsize 4
X X O O
X X . O
O . O O
O O O O

bad_selfatari w b2 1
bad_selfatari b b2 0
bad_selfatari w c3 1
bad_selfatari b c3 0

% Triangle six lives
boardsize 5
O O O O O
O . . O O
O X X O O
O X X X O
O O O O O

bad_selfatari b b4 1
bad_selfatari b c4 1

% Seven stones nakade is bad
boardsize 5
O O O O O
O . . O O
O X X X O
O X X X O
O O O O O

bad_selfatari b b4 1
bad_selfatari b c4 1

% Bulky five nakade
boardsize 5
X X X X X
X O O X X
X O . X X
X X . X X
X X X X X

bad_selfatari b c3 0
bad_selfatari w c3 0
bad_selfatari b c2 1
bad_selfatari w c2 1

% Bulky five nakade (outside lib)
boardsize 5
X X X X X
X O O X X
X O . X X
X X . X X
X X X X .

bad_selfatari b c3 0
bad_selfatari w c3 0
bad_selfatari b c2 1
bad_selfatari w c2 1

% Capture-from-within 3pt-eye (straight) nakade
boardsize 3
O O O
. X .
O O O

bad_selfatari w a2 1
bad_selfatari b a2 0
bad_selfatari w c2 1
bad_selfatari b c2 0

% Capture-from-within 3pt-eye (bent) nakade
boardsize 3
O . O
O X .
O O O

bad_selfatari w b3 1
bad_selfatari b b3 0
bad_selfatari w c2 1
bad_selfatari b c2 0

% Capture-from-within 4pt-eye (square) nakade
boardsize 3
O . X
O . X
O O O

bad_selfatari w b3 1
bad_selfatari b b3 0
bad_selfatari w b2 1
bad_selfatari b b2 0

% Eye falsification nakade
boardsize 3
O O O
. . X
O O O

bad_selfatari w a2 1
bad_selfatari b a2 1
bad_selfatari w b2 0
bad_selfatari b b2 0

% Bulky five multi-w-group nakade
boardsize 5
X X X X X
X O O X X
X O . . X
X X O X X
X X X X X

bad_selfatari b c3 0
bad_selfatari w c3 0 
bad_selfatari b d3 1
bad_selfatari w d3 0	# Captures !

% Multi-b-group nakade
boardsize 5
. X X . .
X O O X .
X O . X X
X X . X X
. . X X X

bad_selfatari b c3 0
bad_selfatari w c3 0
bad_selfatari b c2 0
bad_selfatari w c2 1
bad_selfatari b d5 0
bad_selfatari w d5 1
bad_selfatari b e4 0
bad_selfatari w e4 1
bad_selfatari b e5 0
bad_selfatari w e5 0

% Real multi-b-group nakade
boardsize 6
X X X O O .
X O . X O .
X . O X O .
O X X X O .
O O X X O .
O O O O O .

bad_selfatari b b4 1
bad_selfatari w b4 0
bad_selfatari b c5 1
bad_selfatari w c5 0

% Almost multi-b-group nakade
boardsize 6
X X X O O .
X O . X O .
X . O X O .
O X X X O .
O . X X O .
O O O O O .

bad_selfatari b b4 0
bad_selfatari w b4 0	# Have to allow this or we may never be able to kill that group
bad_selfatari b c5 0
bad_selfatari w c5 0

% Almost multi-b-group nakade (mirrored)
boardsize 6
. O O O O O
. O X X . O
. O X X X O
. O X O . X
. O X . O X
. O O X X X

bad_selfatari b d2 0
bad_selfatari w d2 0
bad_selfatari b e3 0
bad_selfatari w e3 0

% Eyeshape-avoidance nakade 1
boardsize 4
X X X X
X O . X
X X . X
X X X X

bad_selfatari w c3 0
bad_selfatari w c2 1

% Eyeshape-avoidance nakade 1 (outside lib)
boardsize 4
X X X X
X O . X
X X . X
. X X X

bad_selfatari w c3 0
bad_selfatari w c2 1

% Eyeshape-avoidance nakade 2
boardsize 4
X X X X
X O . O
X X . X
X X X X

bad_selfatari w c3 0
bad_selfatari w c2 1

% Eyeshape-avoidance nakade 2 (outside lib)
boardsize 4
X X X X
X O . O
X X . X
. X X X

bad_selfatari w c3 0
bad_selfatari w c2 1

% Eyeshape-avoidance nakade 3
boardsize 4
X X X X
X O . O
X X X .
X X X X

bad_selfatari w c3 0
bad_selfatari w d2 1

% Eyeshape-avoidance nakade 3 (outside lib)
boardsize 4
X X X X
X O . O
X X X .
. X X X

bad_selfatari w c3 0
bad_selfatari w d2 1

% False nakade
boardsize 5
X . X X .
X O O X X
X O O X .
X . O X X
X O O X X

bad_selfatari w b2 1
bad_selfatari b b2 1
bad_selfatari w b5 1
bad_selfatari b b5 0

% Bad nakade (real game 1)
boardsize 9
O . O . . O X X .
. O . O . O O X .
O O . . O O X X O
X O O O O X O O O
X X O X X X X X O
. X O O O X X X X
. X O X X X . . X
X X X O . . X X X
X O . . O . . . O

bad_selfatari w j8 1
bad_selfatari b j8 0

% 3 stones bad_selfatari nakade to dead shape (middle)
boardsize 5
X X X X X
O . O . X
X X X X X
X X X X X
X X X X X

bad_selfatari w b4 0	# Fill eye
bad_selfatari b b4 0	# Capture and live
bad_selfatari b d4 1   

% 3 stones bad_selfatari nakade to dead shape 
boardsize 5
X X X X X
O O . . X
X X X X X
X X X X X
X X X X X

bad_selfatari w c4 0	# Fill eye
bad_selfatari b c4 0	# Capture and live
bad_selfatari b d4 1   

% 3 stones bad_selfatari nakade to dead shape (outside libs)
boardsize 5
X X X X X
O O . . X
X X X X X
. . . . .
. . . . .

bad_selfatari w c4 0	# Fill eye
bad_selfatari w d4 1   

% 2 stones bad_selfatari nakade to dead shape 
boardsize 4
X X X X
O . . X
X X X X
X X X X

bad_selfatari w b3 0	# Fill eye
bad_selfatari b b3 0	# Capture and live
bad_selfatari b c3 1   

% 2 stones bad_selfatari nakade to dead shape (outside libs)
boardsize 4
X X X X
O . . X
X X X X
. . . .

bad_selfatari w b3 0	# Fill eye


% Bulky-five nakade (outside libs)
boardsize 6
X X X X X X
O O O O X X
. . X O X .
X X X O X X
O O O O X .
O O . . X X

bad_selfatari b b4 0
bad_selfatari b a4 1

% 4 stone nakade
boardsize 4
X O O O
X X . X
X X . X
X X X X

bad_selfatari w c3 0
bad_selfatari w c2 1

% 4 stone nakade (outside libs)
boardsize 4
X O O O
X X . X
. X . X
. X X X

bad_selfatari w c3 0
bad_selfatari w c2 1

% Bad nakade (not taking away eyeshape and not atari)
boardsize 4
O O . X
X . X X
X X X .
. . . .

bad_selfatari w c4 1 

% Not a nakade !  (threatening capture)
boardsize 4
X . O O
X X O O
O . . X
. . . .

bad_selfatari b b4 1	# Can escape !
bad_selfatari w b4 1	# Can escape !

% Not a nakade !  (threatening nothing)
boardsize 4
X . O O
X X O O
O . . .
. . . .

bad_selfatari b b4 1	# Can escape !

% Not a nakade !  (threatening nothing)
boardsize 5
O O O O O
O X O O O
O X . O O
O X X O O
O O . . .

bad_selfatari b c3 1	# Can escape !

% Corner nakade (shortage of libs)
boardsize 4
. O X .
X O . X
X O O O
X X X .

bad_selfatari b c3 0
bad_selfatari b d4 0


##############################################################################
# Seki

% Seki destruction
boardsize 4
X X X .
X O O O
X . X X
X X X X

bad_selfatari w b2 1
bad_selfatari b b2 1
bad_selfatari w d4 1
bad_selfatari b d4 1

% Seki destruction (2 stones)
boardsize 6
. . O X . O
. O O X . O
. O X O O O
. O X X X X
. O X . X .
. O X . X .

sar b e6 1
sar w e6 1
sar b e5 1
sar w e5 1

% Seki destruction (3 stones)
boardsize 6
. . O X . O
. . O X . O
. O O X O O
. O X O O X
. O X X X X
. O X . X .

sar b e6 1
sar w e6 1
sar b e5 1
sar w e5 1

% Forbidden throw-in to get seki
boardsize 5
. X X X .
X X O O O
X O O . .
X O O O O
X X X X X

! bad_selfatari b d3 0	# Gosh, sometimes bad moves are good
! bad_selfatari b e3 0


##############################################################################
# Snapback

% Not-quite-snapback
boardsize 5
X X X X O
X X X . O
X X X . O
O O O X X
O O . . .

bad_selfatari b c1 0
bad_selfatari w c1 1
bad_selfatari b d3 0
bad_selfatari w d3 1
bad_selfatari b d4 1
bad_selfatari w d4 0

% Snapback
boardsize 4
X X O .
. . X .
O X . .
. . . .

bad_selfatari w b3 0	# Snapback !
bad_selfatari b a1 1
bad_selfatari w a1 0
bad_selfatari b a3 1
bad_selfatari w a3 0
bad_selfatari b b3 0
bad_selfatari b d4 0
bad_selfatari w d4 0    # ?

% Snapback
boardsize 5
. X O . .
X . . O .
X O O X .
. X X X .
. . . . .

bad_selfatari b c4 0
snapback b c4 1
snapback b d5 0

% We only check for local snapback, not countercaptures
boardsize 5
. X O . .
X . . O .
O O O X X
X O O O X
X X X X .

bad_selfatari b c4 0
snapback b c4 1

% Snapback capture
boardsize 5
. X O . .
X . X O .
X O O X .
. X X X .
. . . . .

bad_selfatari w b4 1

% Not snapback (3lib group)
boardsize 5
. X O . .
X . . O .
X O O X .
. . X X .
. . . . .

bad_selfatari b c4 1
snapback b c4 0

% Not snapback (extra lib after capture)
boardsize 5
. X O . .
. . . O .
X O O X .
. X X X .
. . . . .

bad_selfatari b c4 1
snapback b c4 0

% Not snapback (multiple captures)
boardsize 6
O O O . . .
O X O . . .
X . . O . .
X O O X . .
. X X X . .
. . . . . .

bad_selfatari b c4 1
snapback b c4 0

% Not snapback (3lib group nearby)
boardsize 5
. X X . .
O O X O .
. . O O .
O O X . .
X X X . .

bad_selfatari b b3 0	# throw-in
snapback b b3 0

% Ko throwin, not snapback
boardsize 5
. . . . .
. X O . .
X . . O .
. X O . .
. . . . .

bad_selfatari b c3 1
snapback b c3 0

% Ko capture
boardsize 5
. . . . .
. X O . .
X O . O .
. X O . .
. . . . .

bad_selfatari b c3 0
snapback b c3 0

% Side snapback
boardsize 5
X X X . .
. O X . .
. O X . .
O X X . .
. . . . .

bad_selfatari b a3 0
snapback b a3 1

% Not side snapback
boardsize 5
. X X . .
. O X . .
. O X . .
O X X . .
. . . . .

bad_selfatari b a3 0
snapback b a3 0

% Bad throw-in, no snapback
boardsize 5
. X X X .
. . O X .
. O O X .
O X X X .
. . . . .

bad_selfatari b a3 1
snapback b a3 0

% Inside snapback
boardsize 5
. X O O O
O O . . O
. X O O O
. . X X X
. . . . .

bad_selfatari b c4 0
snapback b c4 1

% Not snapback
boardsize 6
. . . . O .
. . . . O X
. . . O X .
. . . O X .
. . . . O X
. . . . O .

bad_selfatari w f3 0	# throw-in
snapback w f3 0

% Inside snapback with 2 groups
boardsize 6
. X X X X .
. X O O X .
O O . . X .
. X O O X .
. X X X . .
. . . . . .

bad_selfatari b c4 0
snapback b c4 1

% Not snapback (multiple captures)
boardsize 7
. . . . . . .
. X X X X X .
. X O O O X .
O O . . X O O
. X O O O X .
. X X X X X .
. . . . . . .

bad_selfatari b c4 0	# throwin
snapback b c4 0


##############################################################################
# Connect / escape

% Connect and die (damezumari)
boardsize 4
. X O X
O . O X
O X X X
X X . .

bad_selfatari b b3 0
bad_selfatari w b3 1	# Connect and die

% Bad self-atari (3 stones, connect first !)
boardsize 5
O . O O .
O X X O .
O X . X .
O O X X .
. . X X .

bad_selfatari b b5 1

% Bad self-atari (3 stones, can escape) 
boardsize 5
. O O O .
. O . O O
. O X X O
. O X . .
. O O . .

bad_selfatari b c4 1

% Bad self-atari (4 stones, connect first !)
boardsize 6
O O O . O .
O O O X O O
O O O X O O
. O O X . X
. . O O X X
. . . . . .

bad_selfatari b d6 1

% Bad self-atari (connecting 4 stones, connect and die)
boardsize 6
O O O X O .
O O O . O O
O O O X O O
. O O X . X
. . O O X X
. . . . . .

bad_selfatari b d5 1

% Bad self-atari (5 stones, connect first !)
boardsize 6
O O O O O .
O O . X O O
O O X X X O
. O O X . X
. . O O X X
. . . . . .

bad_selfatari b c5 1

% Connect instead of self-atari !
boardsize 5
X X X O .
X . O . .
X X X O .
. O O O .
. . . . .

bad_selfatari w b4 1
bad_selfatari w d4 0

% Filling false-eye
boardsize 6
X X O O . .
X . X O . .
O X X O . O
O O . X X O
. . O X . X
. . O O X X

bad_selfatari b e2 0	# ok to connect ...
bad_selfatari b b5 1	# as long as there are enough liberties

% Filling false-eye suicide
boardsize 5
. . . . .
. O O O O
. O X X O
. O X . X
. O O X X

bad_selfatari b d2 1


##############################################################################
# Side throw-in

% Side throw-in
boardsize 5
. . . . .
. . . . .
. . . O .
. X X O .
. . . X .

bad_selfatari w c1 0

% Not side throw-in (shape missing)
boardsize 5
. . . . .
. . . . .
. . . O .
. . X O .
. . . X .

bad_selfatari w c1 1

% Not side throw-in (shape missing, corner)
boardsize 5
. . . . .
. . . . .
. . O . .
. X O . .
. . X . .

bad_selfatari w b1 1

% Can't throw-in into the corner
boardsize 5
X . . O .
. X . O X
X O . O X
X O . O X
. . . . .

bad_selfatari w a1 1
bad_selfatari w e1 1

% Not side throw-in (bad shape)
boardsize 5
. . . . .
. . . . .
. . . O .
. . X O .
X . . X .

bad_selfatari w c1 1

% Not side throw-in (bad shape, own stones)
boardsize 5
. . . . .
. . . . .
O . X O .
. O X O .
. . . X .

bad_selfatari w c1 1

% Not side throw-in (bad shape, own stones)
boardsize 5
. . . . .
. . . . .
O . X O .
. O X O .
X . . X .

bad_selfatari w c1 1		# XXX !

% Side throw-in (not eye-falsifying)
boardsize 5
. . . . .
. . . . .
O . . O .
O X X O .
O . . X .

bad_selfatari w c1 0		# Can be useful damezumari

% Side 2 stones throw-in
boardsize 5
. . . . .
. . . . .
. . . O .
. X X O .
. O . X .

bad_selfatari w c1 0

% Side 2 stones throw-in
boardsize 6
. . . . . .
. . . . . .
. . . . . .
. . . . O .
. X X X O .
. . O . X .

bad_selfatari w d1 0

% Side 2 stones throw-in
boardsize 5
. . . . .
. . . . .
. . . O .
X . X O .
X O . X .

bad_selfatari w c1 0

% Not side 2 stones throw-in
boardsize 6
. . . . . .
. . . . . .
. . . . . .
. O . . O .
. O X X O .
. . O . X .

bad_selfatari w d1 1

% Not side 2 stones throw-in (open)
boardsize 5
. . . . .
. . . . .
. . . O .
. . X O .
X O . X .

bad_selfatari w c1 1	# XXX

% Not side 2 stones throw-in (connect-out)
boardsize 5
. . . . .
. . . . .
. O . O .
. . X O .
X O . X .

bad_selfatari w c1 1	# XXX

% 2 stones throw-in (atari, outside group with libs)
boardsize 7
. X . O O . O
O O O X X X O
. . X X X . O
. X . X . O O
. . . X . O .
. . . X . O O
. . . . X X .

bad_selfatari b c7 0      # but cutting is good too ...
bad_selfatari b a7 1      # silly

% 2 stones throw-in (atari)
boardsize 5
. X . O O
O O O X O
. X X X O
. X O O O
. X O . X

bad_selfatari b c5 0      
bad_selfatari b a5 1      # silly

% Not a throw-in (corner)
boardsize 4
. . . .
O O . .
X O O O
. X . O

bad_selfatari b c1 1	# Captures 2 groups
bad_selfatari b a1 0

% 3 stones throw-in doesn't take away eye shape
boardsize 6
. . . . . .
. . . . . .
. . . . . .
. . . . O .
X X X X O .
. O O . X .

bad_selfatari w d1 1


##############################################################################
# Middle throw-in

% Single stone throw-in
boardsize 5
. . . X .
O O O X .
O . . O O
O O O X .
. . . X .

bad_selfatari b c3 0
bad_selfatari b b3 1	# Silly

% 2 stones throw-in (atari)
boardsize 7
. . . . . . .
. . . X X X X
O O O X O O O
. X . O O . O
O O O X O O O
. . . X X X X
. . . . . . .

bad_selfatari b c4 0
bad_selfatari b a4 1      # silly

% 2 stones throw-ins
boardsize 6
O O O O X .
O . X . O .
O O O O X X
. O X . . X
X O O O X X
. X . O . .

bad_selfatari b d5 0   # a) in check_throw_in_or_inside_capture()
bad_selfatari b d3 0   # b)
bad_selfatari b c1 1   # c) silly, connect first		 [ d) tested in Capture-from-within 3pt-eye (straight) nakade ]

% 2 stones throw-in (incomplete shape)
boardsize 5
. O X . .
. O . O .
. X O X X
. X O X .
. . O . .

bad_selfatari b c4 0

% 2 stones throw-in with outside stone
boardsize 6
. O X . X .
. O . O O X
. X O X . .
. X O X X X
. . O . . .
. . . . . .

bad_selfatari b c5 0
bad_selfatari b a6 1

% 2 stones throw-in with outside stone (continued)
boardsize 6
. O . O X .
. O . O O X
. X O X . .
. X O X X X
. . O . . .
. . . . . .

bad_selfatari b c5 0
bad_selfatari b c6 1

% Middle throw-in
boardsize 5
. . . . .
. X X O O
. . . X .
. X X O O
. . . . .

bad_selfatari w c3 0

% Not middle throw-in
boardsize 5
. . . . .
. X X O O
X . . X .
. O X O O
. . . . .

bad_selfatari w c3 1

% Not middle throw-in (weird shape)
boardsize 5
. . . . .
. X X O O
X . . X .
. . X O O
. . . . .

bad_selfatari w c3 1
