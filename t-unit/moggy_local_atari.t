# auto-run off

% Moggy local atari testing

% Capture selfatari
boardsize 7
. . . . . . .
. . X O O O .
. . X X X O .
. X . . X O .
. X . O X O .
. X . O X O .
. X . X)O O .

moggy moves 1=C1>90

% Prefer to capture selfatari
boardsize 7
. . . . . . .
. . X O O O .
. . X X X O .
. X . . X O .
. X . O X O .
. X . O X O .
. X . X)O . .

# XXX Connecting instead of capturing selfatari can be really bad
moggy moves 1=C1>60


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

moggy moves !B3 !A1>50


% Defend big group, capture
boardsize 5
. . . X .
. O X O X
. X O O X
. X . O X
. . X)O X

moggy moves 1=(C5|E5)

% Defend big group, capture right one
boardsize 5
. . O X X
. . X O).
. . . X O
. . . . .
. . . . .

moggy moves		# XXX should be B5 ~100%, definitely not E2 ! (selfatari_cousin() bug)

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


