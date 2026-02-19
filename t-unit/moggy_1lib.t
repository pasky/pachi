# auto-run off

# Moggy 1lib testing (local_atari_check())
# (work in progress ...)


% Capture selfatari
boardsize 7
. . . . . . .
. . X O O O .
. . X X X O .
. X . . X O .
. X . . X O .
. . O O X O .
. . . X)O O .

moggy moves 1=C1>90

% Always capture selfatari that can connect out
boardsize 7
. . . . . . .
. . X O O O .
. . X X X O .
. X . . X O .
. X . O X O .
. X . O X O .
. X . X)O . .

moggy moves 1=C1>100		# connecting instead really bad

% Always capture selfatari that can escape
boardsize 7
. . . . . . .
. . X O O O .
. . X X X O .
. X . . X O .
. X . O X O .
. X . O X O .
. . . X)O . .

moggy moves 1=C1>100		# connecting instead really bad

% Always capture selfatari that can escape (play in tiger mouth)
boardsize 7
. . . . . . .
. . . . . . .
. . . O X . .
. . . O X . .
. . . X)O . .
. . . O X . .
. . . . . . .

moggy moves 1=C3>100		# defending instead really bad

% Defend big group
boardsize 5
. . . . .
. . X X X
. . O O X
. . X)O X
. . X O X

moggy moves 1=B3>=100

% Defending = selfatari
boardsize 5
. . . . .
. X X X X
. . O O X
. X X)O X
. . X O X

moggy moves !B3

% Defending = suicide
boardsize 5
. . . . .
. X X X X
X . O O X
. X X O X
. . X)O X

moggy moves !B3


% Defend big group, capture
boardsize 5
. . . X .
. O X O X
. X O O X
. X . O X
. . X)O X

moggy moves 1=(C5|E5)

% Defend big group, avoid snapback
boardsize 5
. . O X X
. . X O).
. . . X O
. . . . .
. . . . .

moggy moves 1=B5>80 !E4

% Defend big groups, choose one
boardsize 7
. . . . . . .
. . . . . . .
. . X . X . .
X O O X)O O X
X O X . X O X
X O X . X O X
. X . . . X .

moggy moves 1=(F5|B5)


