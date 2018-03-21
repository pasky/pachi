<img align="right" src="media/pachi-med.jpg">

Pachi
=====

Pachi can refer to: a simple modular framework for programs playing
the game of Go/Weiqi/Baduk, and a reasonably strong engine built
within this framework.


## Engine

The default engine plays by Chinese rules and should be about 7d KGS
strength on 9x9. On 19x19 it can hold a solid KGS 2d rank on modest
hardware (Raspberry Pi 3, dcnn) or faster machine (e.g. six-way Intel
i7) without dcnn.

When using a large cluster (64 machines, 20 cores each), it maintains
KGS 3d to 4d and has won e.g. a 7-stone handicap game against Zhou Junxun 9p.

By default, Pachi currently uses the UCT engine that combines
Monte Carlo approach with tree search; UCB1AMAF tree policy using
the RAVE method is used for tree search, while the Moggy playout
policy using 3x3 patterns and various tactical checks is used for
the semi-random Monte Carlo playouts.  Large-scale board patterns
are used in the tree search.


## Binary Releases

**Windows**: download binary [release](https://github.com/pasky/pachi/releases)
for windows and follow instructions inside.

**Ubuntu**: there's a 'pachi-go' package in my [ppa](https://launchpad.net/~lemonsqueeze/+archive/ubuntu/pachi):

    sudo add-apt-repository ppa:lemonsqueeze/pachi
    sudo apt-get update
    sudo apt-get install pachi-go

> Performance might be better if you recompile for your own system though.

## Installation

Currently *nix, Mac and Windows are supported. To build Pachi, simply type:

	make

<img align="right" src="media/screenshot_sabaki.jpg" title="playing through sabaki">

The resulting binary program `pachi` is a GTP client. Connect to it
with your favorite Go program interface (e.g. [gogui][1], [sabaki][2], [qgo][3]),
or use [kgsGtp][4] to connect it to KGS.

> DO NOT make the GTP interface accessible directly to untrusted users
> since the parser is not secure - see the [HACKING](HACKING?raw=true)
> file for details.

[1]: https://sourceforge.net/projects/gogui/
[2]: http://sabaki.yichuanshen.de/
[3]: http://qgo.sourceforge.net/
[4]: http://www.michna.com/kgsbot.htm

The pachi program can take many parameters, as well as the particular
engine being used; the defaults should be fine for initial usage,
see below for some more tips.

In case you hit compilation issues (e.g. when building on MacOS/X)
or want to change the build configuration, check the user configurable
section at the top of the Makefile.


## DCNN support

Pachi can use a neural network as source of good moves to consider.
This makes it about 1 stone stronger and makes the games
more pretty. With dcnn support Pachi can also run on modest hardware
with very few playouts, or even no playouts at all using the raw dcnn
engine (not recommended for actual play, pachi won't know when to pass
or resign !).

One drawback however is that pondering and dcnn can't be used at the same
time right now (you should get a warning on startup).

To build Pachi with DCNN support:
- Install [Caffe](http://caffe.berkeleyvision.org)  
  CPU only build is fine, no need for GPU, cuda or the other optional
  dependencies.
- Edit Makefile, set DCNN=1, point it to where caffe is installed and build.

Install dcnn files in current directory.
Detlef Schmicker's 54% dcnn can be found at:  
  http://physik.de/CNNlast.tar.gz

More information about this dcnn [here](http://computer-go.org/pipermail/computer-go/2015-December/008324.html).

If you want to use a network with different inputs you'll have to tweak
dcnn.c to accomodate it. Pachi will check for `golast19.prototxt` and
`golast.trained` files on startup and use them if present when
playing on 19x19.


## How to run

By default Pachi will run on all cores, using up to 200Mb of memory for tree
search and taking a little under 10 seconds per move.  You can adjust these
parameters by passing it extra command line options.

For main options description try:

        ./pachi --help

**Time Settings**

Pachi can smartly deal with a variety of time settings (canadian byoyomi
recommended to maximize efficient time allocation). However, most of these
are accessible only via GTP, that is by the frontend keeping track of time,
e.g. KGS or gogui.

It's also possible to force time settings via the command line (GTP
time settings are ignored then).

For example:

	./pachi -t 20

Will make Pachi use 20s per move.

	./pachi -t =5000:15000 threads=4,max_tree_size=100

This will make Pachi play with max 15000 playouts per move on 4 threads,
taking up to 100Mb of memory (+ several tens MiB as a constant overhead).
It should be about 2d with dcnn and large patterns setup.

	./pachi -t _1200 --no-dcnn threads=8,max_tree_size=3072,pondering

This will make Pachi play without dcnn with time settings 20:00 S.D. with 8 threads,
taking up to 3GiB of memory, and thinking during the opponent's turn as well.

**Logs**

Pachi logs details of its activity on stderr, which can be viewed via
`Tools->GTP Shell` in gogui. Tons of details about winrates, memory usage,
score estimate etc can be found here. Even though most of it available through
other means in gogui, it's always a good place to look in case something
unexpected happens.

`-d <log_level>` changes the amount of logging (-d0 suppresses everything)  
`-o log_file` logs to a file instead. gogui live-gfx commands won't work though.

**Large patterns**

Pachi can also use a pattern database to improve its playing performance.  
You can get it at http://pachi.or.cz/pat/ - you will also find further
instructions there.

**Opening book**

> Mostly useful when running without dcnn (dcnn can deal with fuseki).

Pachi can use an opening book in a Fuego-compatible format - you can
obtain one at http://gnugo.baduk.org/fuegoob.htm and use it in Pachi
with the -f parameter:

	./pachi -f book.dat ...

You may wish to append some custom Pachi opening book lines to book.dat;
take them from the book.dat.extra file. If using the default Fuego book,
you may want to remove the lines listed in book.dat.bad.

**Other Options**

For now, there is no comprehensive documentation of engine options, but
you can get a pretty good idea by looking at the uct_state_init() function
in uct/uct.c - you will find the list of UCT engine options there, each
with a description. At any rate, usually the three options above are
the only ones you really want to tweak.


## Install

After compiling and setting up data files you can install pachi with:

    make install
    make install-data

Pachi will look for extra data files (such as dcnn, pattern, joseki or
fuseki database) in pachi's system directory (/usr/local/share/pachi by
default) as well as current directory. System data directory can be
overridden at runtime by setting `DATA_DIR` environment variable.


## Analyze commands

When running Pachi through GoGui, a number of graphic tools are available
through the `Tools->Analyze commands` window:

- Best moves
- Score estimate
- DCNN ratings ...

It's also possible to visualize best moves / best sequence while Pachi is thinking
via the live gfx commands.

![score estimate](media/screenshot_score_est.png?raw=true "score estimate")
![dcnn colormap](media/screenshot_dcnn_colors.png?raw=true "dcnn colormap")

There are some non-gui tools for game analysis as well, see below.


## Greedy Pachi

> Mostly useful when running without dcnn

Normally, Pachi cares only for win or loss and does not take into
account the point amount. This means that it will play slack endgame
when winning and crazy moves followed with a resign when losing.

It may give you a more pleasurable playing experience if Pachi
_does_ take into account the point size, strives for a maximum
(reasonable) win margin when winning and minimal point loss when
losing. This is possible by using the maximize_score parameter, e.g.:

	./pachi -t _1200 threads=8,maximize_score

This enables an aggressive dynamic komi usage and end result margin
is included in node values aside of winrate. Pachi will also enter
scoring even when losing (normally, Pachi will never pass in that case).
Note that if you pass any 'dynkomi' parameter to Pachi, you will reset
the values set by 'maximize_score'.

Note that Pachi in this mode may be slightly weaker, and result margin
should not be taken into account when judging either player's strength.
During the game, the winning/losing margin can be approximated from
Pachi's "extra komi" or "xkomi" reporting in the progress messages.


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

	./pachi -t =5000 policy=ucb1,playout=light,prior=eqex=0,dynkomi=none,pondering=0,pass_all_alive

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


## Game Analysis

Pachi can also help you analyze your games by being able to provide
its opinion on various positions. The user interface is very rudimentary,
but the ability is certainly there.

There are currently several Pachi interfaces provided for this purpose.

**Winrate Development**

Pachi can evaluate all moves within a given game and show how
the winrates for both players evolved - i.e. who was winning at which
game stage. This is implemented using the `tools/sgf-analyse.pl` script.
See the comment on top of the script about its usage.

**Move Ranking**

Pachi can evaluate all available moves in a given situation
and for each give a value between 0 and 1 representing perceived
likelihood of winning the game if one would play that move. I.e. it can
suggest which moves would be good and bad in a single given situation.

To achieve the latter, note the number of move at the situation you
want to evaluate and run the `tools/sgf-ratemove.sh` script.
See the comment on top of the script about its usage.

**Pattern Move Hinting**

Pachi can show instantenous pattern-based move suggestions very much
like for example Moyo Go Studio (though of course without a GUI).
You can use the Move Ranking method above (tools/sgf-ratemove.sh),
but pass it an extra parameter '-e patternplay'.


## Framework

The aim of the software framework is to make it easy to plug your
engine to the common infrastructure and implement your ideas while
minimalizing the overhead of implementing the GTP, speed-optimized
board implementation, etc.  Also, there are premade random playout
and UCT tree engines, so that you can directly tweak only particular
policies.  The infrastructure is pretty fast and it should be quite
easy for you (or us) to extend it to provide more facilities for
your engine.

See the [HACKING](HACKING?raw=true) file for a more detailed developer's view of Pachi.

Also, if you are interested about Pachi's architecture, algorithms
etc., consider taking a look at Petr Baudis' Master's Thesis:

http://pasky.or.cz/go/prace.pdf

...or a slightly newer scientific paper on Pachi:

http://pasky.or.cz/go/pachi-tr.pdf


## Licence

Pachi is distributed under the GPLv2 licence (see the [COPYING](COPYING?raw=true)
file for details and full text of the licence); you are welcome to tweak
it as you wish (contributing back upstream is welcome) and distribute
it freely, but only together with the source code. You are welcome
to make private modifications to the code (e.g. try new algorithms and
approaches), use them internally or even to have your bot play on the
internet and enter competitions, but as soon as you want to release it
to the public, you need to release the source code as well.

One exception is the Autotest framework, which is licenced under the
terms of the MIT licence (close to public domain) - you are free to
use it any way you wish.
