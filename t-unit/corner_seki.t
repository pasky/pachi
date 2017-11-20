% Corner seki
boardsize 7
. X O O O O O
. X X X O O O
. X . O O X X
. X . O X X .
O O O O X X X
. O O O X . O
O . O O X O).

corner_seki w f2 1
moggy status f1 O


% Corner seki
boardsize 7
. X O O O O O
. X X X O X X
. X . O O X .
. X . O X X X
O O O O X O .
. O O O X O O
O . O O X O).


corner_seki w g3 1
moggy status f1 O


% Fancy corner seki
boardsize 7
. X O O O O O
. X X X O X X
. X . O O X X
. X . O X X .
O O O X X X X
. O O X O . O
O . O X O O).

corner_seki w f2 1
moggy status f1 O


###############################################
# Weird stuff 


% 3 groups, not seki at all
boardsize 7
. X O O O O O
. X X X O O O
. X . O O X X
. X . O X X .
O O O O X X X
. O O . O . O
O . O . X O).

corner_seki w f2 0


% 2 groups don't share same libs ...
boardsize 7
. X O O O O O
. X X X O O O
. X . O O X X
. X . O X X .
O O O O X X X
. O O X X O O
O . . O . O .

corner_seki w e1 0


% No eye ...
boardsize 7
. X O O O O O
. X X X O O O
. X . O O X X
. X . O X X .
O O O O X X X
. O O O X . O
O . O O X . O

corner_seki w f1 0
corner_seki w f2 0


% 2-pt eye ...
boardsize 7
. X O O O O O
. X X X O X X
. X . O O X O
. X . O X X .
O O O O X X X
. O O O X . O
O . O O X O).

!corner_seki w f2 1    # We don't handle that


% False eye, no seki ...
boardsize 7
. X O O O O O
. X X X O O .
. X . O O O X
. X O O X X .
O O O X X X X
. O O X O . O
O . O X O O).

corner_seki w f2 0


% False eye, but seki actually !
boardsize 7
. X O O O X .
. X X O O X X
. X . O O O X
. X O O X X .
O O O X X X X
. O O X O . O
O . O X O O).

!corner_seki w f2 1    # We don't handle weird stuff like that
