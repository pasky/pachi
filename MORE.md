
## Joseki fixes (dcnn)

This module allows to override the main engine moves to fix joseki / fuseki
lines that the dcnn plays poorly, play more modern josekis (`--modern-joseki`
option), or play more varied fusekis as black.

> If you plan to let Pachi play online it's recommended to use this module,  
> otherwise it will likely get abused with all kinds of trick plays =P

It uses Katago as external joseki engine and a database of known positions.
Joseki mistakes can be fixed by editing an SGF file. It can also be used to
make it play variations that it wouldn't play otherwise.

Since Pachi 12.86 it should just work out of the box if you're using the
official releases (KataGo CPU build is provided). If you already have KataGo
installed it's possible to have Pachi use it too.

You can also play directly against KataGo, it's a lot stronger even at low
playouts.

Some older threads which might help too:
  - [Windows](https://github.com/pasky/pachi/issues/154)
  - [Raspberry Pi](josekifix/katago/README)

See josekifix [README](josekifix/README.md) for details.


## Joseki engine (nodcnn)

When playing without dcnn Pachi uses joseki data to improve play during the
opening.

Joseki data comes from the various SGF files in [joseki](joseki) directory,
and are translated into 'joseki19.gtp' which Pachi loads at startup.

If you want to tweak those, read [joseki/README](joseki/README) for a
description of the joseki engine and syntax used.


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


## Memory management

By default Pachi automatically allocates memory for tree search:

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

If you're used to earlier versions of Pachi (< 12.50):
- Not needed to set `max_tree_size` anymore to make long thinking times work
- If you know how much memory you need use `tree_size`
- If you want to limit total memory used use `max_tree_size` or `max_mem`
- `fixed_mem` gives the old behavior (tree memory doesn't grow)


## Large Patterns

Pachi uses MM patterns to guide tree search. The pattern matcher runs
on the cpu each time a new node is explored (see [pattern/README](pattern/README)
for details). Right now prediction rate is about 37%.

One benefit of MM is that the weights are very small. If you used previous
Pachi versions, it's no longer necessary to install extra files to
get patterns working. Patterns should load instantly now and take up
very little memory.


## Distributed Engine

To run Pachi on a cluster of nodes you need to build Pachi with
distributed engine support (not built-in by default).

See [INSTALL](INSTALL.md) for build instructions.
Edit Makefile before building and uncomment these:

    DISTRIBUTED=1
    NETWORK=1

Distributed engine should be available now
(`pachi -e distributed` should hang but not return an error).

To try the distributed engine on the same machine run:

    ./pachi -t =4000  -e distributed slave_port=1234        # master
    
And in another terminal:

    ./pachi -g localhost:1234 slave                         # slave

Now make it generate a move, type in the first window:

    boardsize 19
    clear_board
    genmove b

To use a UI like GoGui or Sabaki give it the command for master.
Wait until Pachi starts then start the slave. Now you should be
able to generate moves within the UI.

To try it on a cluster install Pachi on the different nodes,
run it like (30s per move):

    ./pachi -t 30 -e distributed slave_port=1234           # master
    ./pachi -g masterhostname:1234 slave                   # slaves

And they should all coordinate when asked to generate a move.
By default each slave uses all cores available (1 thread per core).

See distributed/distributed.c for details and more options.


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

