## Memory management

By default Pachi automatically allocates memory for tree search now:

```
auto_alloc     automatically grow tree memory as needed (default)
fixed_mem      don't grow tree memory during search (same as "auto_alloc=0")
               search will stop if initial memory runs out.
	       
tree_size      initial amount of memory allocated for tree search  
               can be useful to save memory / avoid reallocations  
               if you know how much you need.
max_tree_size  max amount of memory tree search can use (default: unlimited)  
               can temporarily use more during tree reallocations.
max_mem        max amount of memory tree search can use  
               like "max_tree_size" but takes tree reallocations into account.
```

Compared to earlier versions of Pachi (<= 12.45):
- Not needed to set `max_tree_size` anymore to make long thinking times work
- If you know how much you need use `tree_size`
- If you want to limit total memory used use `max_tree_size` or `max_mem`
- `fixed_mem` gives old behavior (tree memory doesn't grow)


## Large Patterns

Pachi uses MM patterns to guide tree search. The pattern matcher runs
on the cpu each time a new node is explored (see pattern/README for details).
Right now prediction rate is about 37%.

One benefit of MM is that the weights are very small. If you used previous
Pachi versions, it's no longer necessary to install extra files to
get patterns working. Patterns should load instantly now and take up
very little memory.


## Joseki engine

When playing without dcnn Pachi uses a joseki engine to improve play during
the opening. The "Joseki Moves" gogui analyze command can be used to display
what moves Pachi would consider in a given position. Just keep in mind these
are "Pachi joseki moves": moves Pachi might want to play at around 3k level.
For a full joseki reference from a player's point of view see Kogo joseki
dictionary for example.

To run Pachi without joseki engine:
   `pachi --nodcnn --nojoseki -t =5000`


## Opening book

> Mostly useful when running without dcnn (dcnn can deal with fuseki).

Pachi can use an opening book in a Fuego-compatible format - you can
obtain one at http://gnugo.baduk.org/fuegoob.htm and use it in Pachi
with the -f parameter:

	pachi -f book.dat ...

You may wish to append some custom Pachi opening book lines to book.dat;
take them from the book.dat.extra file. If using the default Fuego book,
you may want to remove the lines listed in book.dat.bad.


## Greedy Pachi

> Mostly useful when running without dcnn

Normally, Pachi cares only for win or loss and does not take into
account the point amount. This means that it will play slack endgame
when winning and crazy moves followed with a resign when losing.

It may give you a more pleasurable playing experience if Pachi
_does_ take into account the point size, strives for a maximum
(reasonable) win margin when winning and minimal point loss when
losing. This is possible by using the maximize_score parameter, e.g.:

	pachi -t _1200 threads=8,maximize_score

This enables an aggressive dynamic komi usage and end result margin
is included in node values aside of winrate. Pachi will also enter
scoring even when losing (normally, Pachi will never pass in that case).
Note that if you pass any 'dynkomi' parameter to Pachi, you will reset
the values set by 'maximize_score'.

Note that Pachi in this mode may be slightly weaker, and result margin
should not be taken into account when judging either player's strength.
During the game, the winning/losing margin can be approximated from
Pachi's "extra komi" or "xkomi" reporting in the progress messages.


## Setting Engine Options Over GTP

- `pachi-setoption <engine_options>`
- `pachi-getoption [<option>]`

<engine_options> format is the same as toplevel pachi command (comma separated).
pachi-setoption adds specified option(s) to current set of engine options.
Options set through pachi-setoption persist across engine resets (clear_board etc).
You should get a gtp error if option is invalid.

Without argument pachi-getoption returns all engine options currently set (comma separated),
given option's value otherwise.

Example:

* `pachi-getoption max_tree_size`

  Get max memory size for tree search.

* `pachi-setoption max_tree_size=1024`

  Change to 1Gb.


## Game Analysis

Pachi can help you analyze your games by being able to provide its
opinion on various positions. The user interface is very rudimentary,
but the ability is certainly there.

There are currently several Pachi interfaces provided for this purpose:

* Winrate Development

  Pachi can evaluate all moves within a given game and show how
  the winrates for both players evolved - i.e. who was winning at which
  game stage. This is implemented using the `tools/sgf-analyse.pl` script.
  See the comment on top of the script about its usage.

* Move Ranking

  Pachi can evaluate all available moves in a given situation
  and for each give a value between 0 and 1 representing perceived
  likelihood of winning the game if one would play that move. I.e. it can
  suggest which moves would be good and bad in a single given situation.

  To achieve the latter, note the number of move at the situation you
  want to evaluate and run the `tools/sgf-ratemove.sh` script.
  See the comment on top of the script about its usage.

* Move Hinting

  Pachi can show instantenous dcnn or pattern-based move suggestions either
  graphically in GoGui or through the above method (pass an extra parameter
  like '-e patternplay' to tools/sgf-ratemove.sh).


## Experiments and Testing

Except UCT, Pachi supports a simple `random` idiotbot-like engine and an
example `montecarlo` treeless MonteCarlo-player. The MonteCarlo simulation ("playout")
policies are also pluggable, by default we use the one that makes use of
heavy domain knowledge.

Other special engines are also provided:
* `distributed` engine for cluster play; the description at the top of
  distributed/distributed.c should provide all the guidance
* `dcnn` engine plays moves according to dcnn policy.
* `replay` engine simply plays moves according to the playout policy suggestions
* `patternplay` engine plays moves according to the learned patterns
* few other purely for development usage

Pachi can be used as a test opponent for development of other go-playing
programs. For example, to get the "plainest UCT" player, use:

	pachi -t =5000 --nodcnn policy=ucb1,playout=light,prior=eqex=0,dynkomi=none,pondering=0,pass_all_alive

This will fix the number of playouts per move to 5000, switch the node
selection policy from ucb1amaf to ucb1 (i.e. disable RAVE), switch the
playouts from heuristic-heavy moggy to uniformly random light, stop
prioring the node values heuristically, turn off dynamic komi, disable
thinking on the opponent's time and make sure Pachi passes only when
just 10% alive stones remain on the board (to avoid disputes during
counting).

You can of course selectively re-enable various features or tweak this
further. But please note that using Pachi in this mode is not tested
extensively, so check its performance in whatever version you test
before you use it as a reference.

Note that even in this "basic UCT" mode, Pachi optimizes tree search
by considering board symmetries at the beginning. Currently, there's no
easy option to turn that off. The easiest way is to tweak board.c so
that board_symmetry_update() has goto break_symmetry at the beginning
and board_clear has board->symmetry.type = SYM_NONE.

