# pass_is_safe() tests
# needs --kgs option (smart pass enabled for japanese rules)

% too early to pass
boardsize 9
komi 0.5
. . . . . . . . .
. . . . X . . . .
. . O . . . . O .
. . . . . . . . .
. . . . . . . . .
. X . . . . . X .
. . . . . . . . .
. . . . X . X . .
. . . . . . . . .

pass_is_safe b 0
pass_is_safe w 0


% dames left but ok
boardsize 9
komi 0.5
. X . O X . X . .
. . . O . O . X .
. . O O O O O O .
. . O . . . X O .
. . O X X X X O .
. . O X O . X O O
O O O X . . X . O
X X O X . O . X .
. X X X X X X X X

pass_is_safe w 1
pass_is_safe b 0   # losing game


% unfinished game, open territories
boardsize 9
komi 0.5
. X . O X . X . .
. . . O . O . X .
. . O O O O O . .
. . O . . . O . .
. . O X X X O . .
. . O X O X O . .
O O O X . X O . .
X X O X . O X X X
. X X X X X X . .

pass_is_safe w 0


% open territories at the edge
boardsize 9
komi 0.5
. X . O X . X . .
. . . O . O . X .
. . O O O O O O .
. . O . . . X O . 
. . O X X X X O .
. . O X O . X O .
O O O X . . X O .
X X O X . O . X X
. X X X X X X X .

pass_is_safe w 0


% non-final position at F3
boardsize 9
komi 0.5
. X . O X . X . .
. . . O X O . X .
X X . X . X O X X
O O X . X . X O O)
. O O X X X X O .
O . O X O O O O O
O O O X . . O . O
X X O X X O O O O
. X X X X X X X X

pass_is_safe b 0


% border stones in atari
boardsize 9
komi 0.5
. X . O X . X . .
. . . O . O O X .
. . O O O X O O .
. . O . X . X O . 
. . O X X . X O .
. . O X O . X O .
O O O X . . X O O
X X O X . O X X X
. X X X . X . . .

pass_is_safe w 0


% smart pass: unclear groups, must guess
boardsize 9
komi 0.5
. . . . . X X . .
. O . . O X . X .
X X X X X X X X X
O O O X X O X O X
. . O X X O O O O
. . O O O O X X O
. . . . O X . X O
. O O O X X . X O
. O X X X . . X O)

rules chinese
pass_is_safe b 0    # smart pass disabled in chinese
pass_is_safe w 0

rules japanese
pass_is_safe b 1    # guesses alive
pass_is_safe w 1    # guesses wrong ! but intended behavior right now


% smart pass: unclear groups, ok in worst-case scenario
boardsize 9
komi 0.5
O O O O O X X . .
X O . O O X . X .
. X O O X X X X X
. . O X X O X O X
. . O X X O O O O
. . O O O O X X O
. . . . O X . X O
. O O O X X . X O
. O X X X . . X)O

rules chinese
pass_is_safe w 0     # smart pass disabled in chinese
pass_is_safe b 0

rules japanese
pass_is_safe w 1
pass_is_safe b 0


% dead group not captured yet !
boardsize 9
komi 0.5
. X X X O O O . O
X . X O O O . O O
X X O X X O O O .
X O O X X O . O O
O O X . X O O O O
O O X . . X O . O
. O O X X X O O O
O . O X X X X X O
O O O X . X). X O

pass_is_safe w 0         # actual test
# XXX w can capture b 2 stones at C4 because of damezumari, however
#     hasn't been played out so can't consider it as dead group yet !
#     actually works because of non-final position at E4...
#     add test for these kind of situations ?

!pass_is_safe b 1         # make it fail on purpose


% double ko !
boardsize 9
komi 0.5
. X . O X . X . .
. . . O X O . X .
X X . X . X O X X
O O X . X . X O O)
. O O X X X X O .
O . O X O O O O O
O O O X O . O X O
X X O O X O X . X
. X X X X X X X X

rules chinese
!pass_is_safe b 1         # XXX fixme !
rules japanese
!pass_is_safe b 1         # XXX fixme !


% nothing special, W+7.5
boardsize 19
komi 0.5
handicap 6
. . X O O . . . . . . . . . O O X . .
. . X X O O . . . . . . . O O X . . .
. . . X X O O O O . O . . O X X X . .
. . . X . X O X O O . O O O X X . X X
X X . . . . X X X O . X . O X . . X X
O X . X . . . X O . . X X O O X X O X
O X . . . . . X O O . O O X X X O O O
O O X X X . X X X O O O X . X O . O .
. . O O X X X X O X . O X X X O . O .
. O O X O O X O O O . O O O X X O . O
. . . . O X X O X X O . . . O X O O X
. . O . O O O O O X X O . O O O O X X
. O . O O X X X X X X X O O X X X X .
. O O X X . . X O X X X X X . X . . .
O O X . . . X X O X O X X X X . . . .
O X X X . . . X O O O O O O O X X . .
X X . . . . X X O . O . . O . O X X .
. . . . . X X O O . . O . . . O O X X
. . . . . X O O . . . . . . . . O O X

rules chinese
pass_is_safe w 1
rules japanese
pass_is_safe w 1


% no time to clarify unclear group at B4 (japanese rules, B+0.5)
boardsize 19
rules japanese
komi 0.5
passes b 2
handicap 3
. . O O O . . O . . O . . . . O O O X
O X O X O O O . . O . O . O O . O X X
. O X X X O O . O O O . O O X O O O X
. O O X X O O O X O X O . O X X X X X
. O . O X . X X X X X . X X O O X . .
. . O X X X X X . . . . X . O . O X .
. O O O O O O X O . O O X O . O O X .
. . O X X O X O O . O X O O O O X X .
. . O X X O X X X . O X O X X O X . .
O O O O X X O O O . O X O O X X X X .
O X X X . X X O . O O X O O X O X . .
X X X . . X O X O O O X O O O O X X X
. O . . . . . X X O X X X O O O X O X
. . O . . . . X X X . X X X X O O O O
. . . . . . . X . . . . . . . X O . O
. O . X . . O O X X X . . X X X O O)X
. O X . . X X X X X X . X . X . X X X
. X . . . . . . X . X . X . X . . . .
. . . . . . . . X . . . X X . . . . .

pass_is_safe b 1


% snapback ok for border stone in atari (H4)
boardsize 19
rules japanese
passes b 1
komi 0.5
handicap 2
X . X X . . X O O . . O O X . . . X .
X . . X X X X X O O . O X X . . X X O
X X X O O X O O O . O X . . X X X O O
O X O . O O O . . O . X . X O X O O .
O O . O O X X O O X X . . X O X X O .
. . O O O O X X X X O X X O O O O O O
. O X X O O X . X O O X X X O . . O O
. O X X X X X . X . O . O O . . . . O
O O O O O O X X O O O O O . . . O O O
. O X X X X . X X O . . . . . O X X X
. O O O O X X O X O O O O O O O X . .
. . X O X X . O O . . O X . O X . . .
. . . O O X X O . O O O X . O X . . .
. . O . X X X O O O X O X X X . X . .
. O O X . X O O X X X O X O X . . X X
. O X X . X O X . X . X O O X X X O O
. . O O X . X O O O X X O O O O O O X
. O O O O X X X O X . X O . . . O O X
. . . O X X . . X . X X X)O . O . O .

pass_is_safe w 1


% unfinished game, lots of unclear groups !
boardsize 19
rules japanese
komi 0.5
. X . X X . O . O . X . . . . . . . .
O O X O O O O . O O X . . . . . . . .
X . X X X X O O X X . . X . O O O O .
. X O O . X X X O X . X . X X X X O .
O X X O X X O O O O O . . . . . X X .
. O O O . O . . . . . . . . . . O X .
. . . . . O . . . . . O . . X . O . .
. . . . . . . . X . O . O O O O . . .
. . . . . . . O X . O . . . . . O . .
. . . . . . . O O X . O . . O . O . .
. . . . . O O X X . X X O . . O . . .
. . . . . O X . X X O O . O O . X . .
. . . O O X X X O X X O . . . . . . .
. . O . X X . X O . O . . . . . . . .
. . . . . O O O O O . . O . . . . . .
. . . O . O X X O X O O X X . . X . .
. . . . O X O X X X X X . . X . . . .
. . . X X X . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . .


pass_is_safe w 0


% non-final position check too strict...
boardsize 19
komi 0.5
handicap 5
. . X O O . . . . . O X X X X . X . .
. X . X O O . . . O O X O O O X . X .
. . . X X O . . . O X . X O X X X . .
. . . X O O . O O O X X X O O X X . .
X X . X O O O O X X X X O . O X . . .
O X X X X X O X X . X O O . O X X . .
O O O O X X O O X . . X O . . O X . .
. . O X X O O X . X . X O O . O X . .
O . O O O X O X X . . X O . . O X . .
. O . O X X O O O X . X O O O X . X X
. O O X X O O . O X X X O X X X . . .
O O X X X X X O O O O O X O . X . X X
. X . X O O X X X X . O X X X X . . X
X X X X O . O O O X X O O O O O X X X
O O O X O X . O O O O O . O X X O X .
. . O X O O O X)X O X X O O O X . . .
O O O X X O . . X X X . X O O X . . .
. O O X O O O X X . X X X X O X X . .
O O X X O O O O X . . . . X O O X . .

# XXX getting non-final position at G3, silly
#     check eye shape ?
!pass_is_safe w 1


% smart pass: can't pick b&w dead groups next to each other!
boardsize 19
komi 6.5
. . . . . . O . O O O X X X X X . X .
. O X X X X O . O X X X X O X X X . .
. O O X O X X O O O O X O O O O X X .
. O X O O X O O . O X O O . O . O X .
. . . . . O X . O O X . . X . O O X .
. . O . . O X . O X X X . X O O . O X
. . . . . O X . X O X X X X X X O O .
. . . . . O X . X O O O O O . X X O X
. . . O O X X . . . . O X X X X X X X
. . . O X O . . X O O . O X O O O O O
O O . . X X O O O X O O O O O . O X X
X O O O O O O O X X X X X O . O O X .
X X X X X X O X X O X . X O O O X X .
X . X O O O X X O O X X . X O O X . .
X O X X O X O O O . O O X X O O X . .
X X O O O X X O . O O X X X O X . . .
O O O X X X X O . O X X X O O X . . .
. O X X . . X X O . O O X O X X . . .
. X . . . . X O O O . O X X). . . . .

# (artificial example)
# unclear groups at bottom left, only thing
# that works is if he picks both groups as
# dead, which should not be allowed.
rules japanese
pass_is_safe w 0
