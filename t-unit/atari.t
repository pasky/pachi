
##################################################################
# Atari ladder_cut

% Atari ladder_cut
boardsize 7
. . . . O . .
. X . . O . .
. X . X O O O
. . . O X X .
. X X O X . X
O O O O X . X
. O . O X X .

atari w C5  atari:ladder_cut


% Not ladder_cut (dead stone)
boardsize 7
. . . . O . .
. O . . O . .
. . . X O O O
. O . O X X .
. . . O X . X
O O O O X . X
. O . O X X .

atari w C5  atari:ladder


##################################################################
# Atari ladder_safe

% Atari ladder_safe
boardsize 7
. . . . O . .
. . . . O . .
. O . O O O O
. . . O X X .
. O X O X . X
. X X O X . X
. . . O X X .

atari b B4  atari:ladder_safe


##################################################################
# Atari and cap (3 libs kind)

% Atari and cap
boardsize 7
. . . . . . .
. . . . . . .
. . . . O). .
. . X O X X .
. . . . . . .
. . . . . . .
. . . . . . .

atari b D5  atari:and_cap


% Atari and cap
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. O . . . O X
. X O O O X X
. X X X X . .
. . . . . . .

atari b E4  atari:and_cap


% Not atari and cap (capturing dead stones)
boardsize 7
. . . . . . .
. X X X X X .
X . . . . . X
X O . . . O X
. X O O O X X
. X X X X . .
. . . . . . .

atari b E4  !atari:and_cap


% Not atari and cap (selfatari)
boardsize 7
. . . . . . .
. . . . . . .
. . X . O . .
. . . . . O X
. X O O O X X
. X X X X . .
. . . . . . .

atari b E4  !atari:and_cap


% Not atari and cap (can ladder first group)
boardsize 7
. . . . . . .
. . . . . . .
. . . . O). .
. . X O X X .
. . X . . . .
. . . . . . .
. . . . . . .

atari b D5  !atari:and_cap


% Not atari and cap (2nd group not cutting stones)
boardsize 7
. . . . . . .
. . . . . . .
. X X . X . .
X O O . O . .
. . . . X . .
. . . O O O .
. O . . . . .

atari b D4  !atari:and_cap


% Not atari and cap (not considered cutting stones right now ...)
boardsize 7
. . . . . . .
. . X X . . .
. X . . X . .
X O O . O . .
O X . . X . .
. . X O O O .
. O O . . . .

atari b D4    !atari:and_cap


##################################################################
# Atari and cap (2 libs kind)

% Atari and cap (capture 2 libs group nearby)
boardsize 9
. . . . . O X . .
. . . . . O X . .
. . O . . O X . .
. . . O O O X X .
. . O . . . O X .
O O O . . . O X .
X X X O O X O X .
. . X X O O X X .
. X . X X X X . .

atari b e4 atari:and_cap2		# captures 3 stones in snapback


% Not atari:and_cap (can already capture)
boardsize 9
. . . . . O X . .
. . . . . O X . .
. . O . . O X . .
. . . O O O X X .
. . O . X . O X .
O O O . . . O X .
X X X O O X O X .
. . X X O O X X .
. X . X X X X . .

atari b e4 !atari:and_cap2		# can already capture G5 to start with ...


% Atari and cap (capture 2 libs group nearby)
boardsize 9
. . . . . . . . .
. . . . . X . . .
. . . O X O X X .
. O O)X O . X . .
. . . X . . O X .
O O O O O . O X .
. O X X X X O X .
O O O O X X X O .
. O . X X X X . .

atari b e5 atari:and_cap2		# nice


% Atari and cap (capture 2 libs group nearby, but not cutting stones ...)
boardsize 9
. . . . . . . . .
. . . . . X . . .
. . . O X O X X .
. O O)X O . X . .
. . . X . . O X .
O O O O O . O X .
. O X X X X O X .
O O O O X X X . .
. O . X X X X . .

atari b e5 !atari:and_cap2		# unfortunate in this case


% Almost atari and cap (capture 2 libs group nearby)
boardsize 9
. . . . . . . . .
. . . . . . . . .
. . . . X . . X .
. O . X O . X . .
. . . . . . O X .
O O O O O . O X .
. O X X X X O X .
O O O O X X X O .
. O . X X X X . .

atari b e5 !atari:and_cap2		# not atari_and_cap right now because first group can be laddered


% Not atari:and_cap (can already capture)
boardsize 9
. . . . . . . . .
. . X O . X . . .
. X X . O . . X .
. O . X O . X . .
. O . O . . O X .
O O X X . . O X .
. O X X X X O X .
O O O O X X X X .
. O . X X X X . .

atari b e5 !atari:and_cap2


##################################################################
# Atari ladder_big

% Atari ladder_big
boardsize 7
. X . X O . .
. . . X O O .
. . O X O . O
. X X O . O X
. . X O O X X
. . X O X . X
. X . O X X .

atari w c6  atari:ladder_big

% Not ladder_big (dead stones)
boardsize 7
. X . X O . .
. . . X O O .
. . O X O . O
O O O O . O X
. . . O O X X
. . . O X . X
. . . O X X .

atari w c6  atari:ladder

% Atari ladder_big (throwin)
boardsize 7
. . . . . X .
. . O . O X .
. O X O O X .
. O X X X X X
. . O O X . X
. O . O X . X
. . O O X X .

atari b d6  atari:ladder_big


##################################################################
# Double atari

% Double atari
boardsize 7
X X . . . . .
O . X X . . .
. O X . . . .
O X . . . O X
O O O . O X X
O . . O X . X
. O O . X X .

atari b E4  atari:double
atari b B6  atari:some		# not double atari


% Not double atari (selfatari)
boardsize 7
. . . . . . .
. . . . . . .
. . . . . . .
. . . O . O X
O O O . O X X
O . . O X . X
. O O . X X .

atari b E4  -1

% Not double atari (suicide)
boardsize 7
. . . . . . .
. . . O O . .
. . . O X O X
. . . O . O X
O O O . O X X
O . . O X . X
. O O . X X .

atari b E4  -1


##################################################################
# Snapback

% Simple snapback
boardsize 9
. O). . . O X . .
. . O . . O X . .
O O O . . O X . .
X X O O . O X . .
. X X O O X X X .
. . X O O X X O .
. X . O X X O O .
. X X O X . X O .
. . X O O . X O .

atari w f2  atari:snapback

% Snapback
boardsize 9
. O . . . O X . .
. . O . . O X . .
O O O . . O X . .
X X O O . O X . .
. X X O O X X X .
. . X X O O X O .
. . X O X X O O .
. . X O O X X O .
. O)X . O . . X .

atari w g1  atari:snapback

% Snapback
boardsize 9
. O). . . O X . .
. . . . . O X . .
O O O . . O X . .
X X O O . O X . .
. X X O O X X X X
. . X X O O X O X
. . X O O O O O X
. . X O X X X O X
. . X O X . . X X

atari w g1  atari:snapback

% Not snapback (can countercap)
boardsize 9
. O). . . O X . .
. . O . . O X . .
O O O . . O X . .
X X O O . O X . .
. X X O O X X X .
. . X O O X X O .
. X . O X X O . .
. X X O X . X O .
. . X O O . X O .

atari w f2  -1


% Not snapback (capture 2 groups)
boardsize 9
. O). . . O X . .
. . O . . O X . .
O O O . . O X . .
X X O O . O X . .
. X X O O X X X .
. . X O O X X O .
. X . O X X O O .
. X X O X . X O .
. . X X O . X O .

atari w f2  -1


% Not snapback (just connects)
boardsize 9
. O). . . O X . .
. . O . . O X . .
O O O . . O X . .
X X O O . O X . .
. X X O O X X X .
. . X O O X X O .
. X . O X X O O .
. X X O X . X O .
. . X X X . X O .

atari w f2  -1


