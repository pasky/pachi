/* This is a Pachi UCT plugin for Thomas Wolf's move evaluation API. */
/* This file is released under the same licence conditions as
 * <uct/plugin.h>. */

/* We will add positive priors (1.0) for moves that play in-between
 * of two different groups of the same color; that is, moves that connect
 * two groups or the same color or separate two groups of the same color.
 * This is not a very good prior actually, since it leads to a lot of
 * useless moves. (Maybe doing this in simulations would be more interesting?)
 * But it is a simple enough example. :-) */

/* Compile the plugin like this:
 * gcc -Wall -O3 -march=native -Ipachi_source_root -shared -fPIC -o wolf.so wolf.c
 * Then, load it in Pachi by passing plugin=wolf.so as a parameter.
 * You can also pass it parameters: plugin=wolf.so:p1=v1:p2=v2.
 * The plugin supports these parameters:
 * file		Filename of the real module used
 * eqex		Number of prior'd simulations, overrides Pachi default
 * threshold	Threshold value when to stop iterating influence/strength values.
 * overrelax	Overrelaxation parameter. Should probably not be changed from 1.0
 * iterations	Upper bound of the number of iters for each empty point or chain.
 */


/* This is the Thomas Wolf's API we implement:

The library currently provides 5 functions:

 SETPARAM 
 EVALFUN1
 EVALFUN2
 FINDMOVE1
 FINDMOVE2

of which SETPARAM, EVALFUN2, FINDMOVE2 are the ones likely to be used
in other programs.

- - - -

SETPARAM 

This procedure has to be called before any EVALFUN call but can also be
called again later. It sets parameters for the computation of the
influence function.

The first parameter is a threshold value when to stop iterating
influence/strength values. Characteristics: The smaller the value the
more accurate the influence value but the slower the computation due
to an increase in iterations. But because accuracy is limited by
systematic errors, too small values do not bring improvement,
especially not in unstable situations.

The second is an overrelaxation parameter. Should probably not be
changed from 1.0 .

The third is an upper bound of the number of iterations for each empty
point or chain. Again, the more the better but also the slower. Again,
improvement is overshadowed by systematic errors, especially in
unstable situations.  Typical values have the range 3..65000.

- - - -

EVALFUN1, EVALFUN2

Both are one and the same static evaluation function, only with
different interface. 

Input:

For both functions the first parameter is a string
which provides the necessary input by specifying the board position,
who moves next, specifying whether the evaluation function should
be applied ad hoc (task=3) or incrementally (task=4) and which 
optionally allows to specify unconditionally alive chains. The 
syntax of the input for EVALFUN1 and EVALFUN2 is the following:

- Only for ad hoc mode:
  A single digit specifying the size of the board:
   1 --> 19x19, 2 --> 17x17, 3 --> 15x15, 4 --> 13x13,
   5 --> 11x11, 6 -->  9x9,  7 -->  7x7,  8 -->  5x5
- Only for ad hoc mode:
  All stones on the board.
  Because there are more than 256 fields and sending non-alphanumeric
  characters may give problems we need 2 coordinates for each stone
  anyway. In these coordinates (1,1) is in the lower left corner 
  and the 1st increases to the right and the 2nd increases vertically
  to the top.
  Black stone coordinates are encoded by chr(64+x), so 'A' would 
  be 1, 'B' be 2,... and white stone coordinates by chr(96+x), so
  'a' is 1, 'b' is 2, ...
- If there is a field forbidden by ko then the first coordinate
  is a capital letter and the second a lower case letter.
- Next is a single character '%' indicating the end of the sequence of fields.
- Next is a single character specifying who moves first and form of solution:
  'b' Black moves first                                                 <*>
  'w' White moves first                                                 <*>
- The task: a single digit that specifies the task.
  Available currently:
3 ... evaluation of all chains and empty fields within a region, adhoc version.
  If the region is the whole board no further data follow.
  If the region is bounded then the following data consist of points (x,y).
  Each point is encoded by 2 byte, either as chr(64+x) chr(64+y), i.e. as
  upper case letters or as chr(96+x) chr(96+y), i.e. lower case letters.
  Whether upper or lower case does not depend on the colour (the colour of the
  stones has already been specified above) but is defined as follows:
  - The first point (occupied or not) marks the inside of the region to be
    evaluated. For that one can use upper or lower case letters.
  - Each one of any further points marks a chain that shall be assumed to be
    alive, i.e. such a chain will be a boundary to the region to be evaluated.
    If a field marking a chain is encoded in upper case letters by 
    chr(64+x) chr(64+y) then the chain is assumed to be statically alive which
    is a property that will be respected in future calls of task 4 below. If
    the chain marking field is encoded in lower case letters then it treated
    as alive for now in this computation of the evaluation function but not
    necessarily in future calls of task 4.
 
4 ... evaluation of all chains and empty fields within a region, incremental version.
 Input is exactly exactly like task 3. The difference to task 3 is that this
 position has already been evaluated in the previous call with task 3 or 4.
 Therefore, no boardsize, no stones and no ko-field are specified. That means
 the complete input is:
 - The first character is '%'
 - followed by 'b' or 'w' to mark the colour moving next,      <*>.
 - a digit for the task, so 4,
 - optional, like in task 3: fields marking alive chains,
 - a '%' character to mark the end of the optional list of alive chains
 - a point (x,y) encoded by 2 byte chr(64+x) chr(64+y) that marks
   the coordinates of the next move of the colour given in <*>.

Output:

EVALFUN1 has a simple interface, providing with its 2nd parameter a 
string that gives for each board intersection 
- the influence value of Black (in the range 0 .. 1.0) if the 
  intersection is empty
- the strength value of the chain occupying that intersection
  (in the range 0.0 = dead .. 1.0 = uncnditionally alive) if the 
  intersection is not empty.
This interface is simple but slow because this string has to be 
generated and to be decoded to use any information.

EVALFUN2 gives direct access to the relevant data. 
- For an intersection with coordinates (x,y) (1<=x<=boardsize from 
  left to right, 1<=y<=boardsize from bottom to top) the 2-dim
  array PChainNo(x,y)
  - is 0 if the intersection is empty,
  - gives the chain number of the chain that occupies (x,y).
- For an empty intersection with coordinates (x,y) (1<=x<=boardsize from 
  left to right, 1<=y<=boardsize from bottom to top) the record/structure  
  PPD(i,j) stores relevant data and boc is the influence value of Black.
- For an intersection (x,y) occupied by a chain with number n the 
  record/structure PCD(n) stores the relevant data of this chain
  and the svl component gives a strength value in the interval 
  0.0 (= unconditionally dead) to 1.0 (= unconditionally alive).

- - - -

FINDMOVE1, FINDMOVE2

Before calling FINDMOVE1/2 the evaluation function EVALFUN1 or
EVALFUN2 for the current board position has to be executed.

Both, FINDMOVE1 and FINDMOVE2, are one and the same function that
provides a ranking of legal moves, only with different
interface. Currently FINDMOVE performs each legal move, updates the
static evaluation and adds up probabilities over the whole board to
arrive at a rough score for each move. Thus, depending on the number
of legal moves this function is up to 360 times slower than EVALFUN in
update mode.

- The first parameter is specifying which sides moves next: White=-1, Black=1.
- The 2nd parameter of FINDMOVE1 is an output string giving for each legal
  intersection a measure of value of doing the next move there.
  The first move is the supposedly best move. All other moves are 
  listed line by line of the board.
- For FINDMOVE2 the 2nd and 3rd parameter are the x- and y-coordinate
  of the best move and the 4th parameter is a 'value' of that move.
  The 5th parameter is a 2-dim array which for each intersection
  gives a 'value' of moving there. To identify whether the intersection is
  empty the last parameter is a 2-dim array giving a value 0 if it is
  empty (otherwise the number of the chain that occupies the intersection).

- - - -

 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

/* The basic plugin interface. */
#include "uct/plugin.h"

/* The API types: */
#define MAXBOARDSIZE 19
typedef char byte_board[MAXBOARDSIZE+2][MAXBOARDSIZE+2]; // The array indices are 1-based!
typedef floating_t influ_board[MAXBOARDSIZE][MAXBOARDSIZE];

/* Our context structure. */
struct context {
	int eqex;

	void *dlh;
	void (*SETPARAM)(double sv, double omega, uint16_t mi);
	void (*EVALFUN1)(char *javp, char *InfluField);
	void (*FINDMOVE2)(int fa, char *mi, char *mj, floating_t *mxscore, influ_board *SB, byte_board **PChainNo);
};


char
bsize2digit(int size)
{
	switch (size) {
		case 19: return '1';
		case 17: return '2';
		case 15: return '3';
		case 13: return '4';
		case 11: return '5';
		case 9: return '6';
		case 7: return '7';
		case 5: return '8';
		default:
			fprintf(stderr, "wolf plugin: Unsupported board size: %d\n", size);
			exit(1);
	}
}

char
coord2digit(enum stone color, int coord)
{
	assert(color == S_BLACK || color == S_WHITE);
	return (color == S_BLACK ? 64 + coord : 96 + coord);
}

void
pachi_plugin_prior(void *data, struct tree_node *node, struct prior_map *map, int eqex)
{
	struct context *ctx = data;
	struct board *b = map->b;
	if (ctx->eqex >= 0)
		eqex = ctx->eqex; // override Pachi default

	/* First, create a string representation of current board. */
#define BIGSTR 10000
	char bin[BIGSTR];
	char bout[BIGSTR];
	char *bip = bin;
	*bip++ = bsize2digit(board_size(b) - 2);
	foreach_point(b) {
		enum stone s = board_at(b, c);
		if (s == S_NONE || s == S_OFFBOARD) continue;
		*bip++ = coord2digit(s, coord_x(c, b));
		*bip++ = coord2digit(s, coord_y(c, b));
	} foreach_point_end;
	if (!is_pass(b->ko.coord)) {
		*bip++ = coord2digit(S_BLACK, coord_x(b->ko.coord, b));
		*bip++ = coord2digit(S_WHITE, coord_y(b->ko.coord, b));
	}
	*bip++ = '%';
	*bip++ = map->to_play == S_BLACK ? 'b' : 'w';
	*bip++ = '3';
	*bip = 0;

	/* Seed the evaluation of the situation. */
	// fprintf(stderr, "board desc: %s\n", bin);
	ctx->EVALFUN1(bin, bout);
	/* We do not care about bout. */

	/* Retrieve values of moves. */
	char bestx, besty;
	floating_t bestval;
	influ_board values;
	byte_board *chaininfo;
	ctx->FINDMOVE2(map->to_play == S_BLACK ? 1 : -1, &bestx, &besty, &bestval, &values, &chaininfo);
	// fprintf(stderr, "best is (%d,%d)%s %f\n", bestx, besty, coord2sstr(coord_xy(b, bestx, besty), b), bestval);

	/* In the first pass, determine best and worst value. (Best value
	 * reported by FINDMOVE2 is wrong.) In the second pass, we set the
	 * priors by normalization based on the determined values. */
	floating_t best = -1000, worst = 1000;
	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;
		floating_t value = values[coord_x(c, b) - 1][coord_y(c, b) - 1];
		if (map->to_play == S_WHITE) value = -value;
		if (value > best) best = value;
		else if (value < worst) worst = value;
	} foreach_point_end;

	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;

		/* Take the value and normalize it somehow. */
		/* Right now, we just do this by linear rescaling from
		 * [worst, best] to [0,1]. */
		floating_t value = values[coord_x(c, b) - 1][coord_y(c, b) - 1];
		if (map->to_play == S_WHITE) value = -value;
		value = (value - worst) / (best - worst);
		// fprintf(stderr, "\t[%s %s] %f/%f\n", stone2str(map->to_play), coord2sstr(c, b), value, best);

		add_prior_value(map, c, (value - worst) / best, eqex);
	} foreach_free_point_end;
}


void *
pachi_plugin_init(char *arg, struct board *b, int seed)
{
	struct context *ctx = calloc(1, sizeof(*ctx));

	/* Initialize ctx defaults here. */
	char *file = NULL;
	floating_t overrelax = 1.0, threshold = 0.001;
	int iterations = 13;
	ctx->eqex = -1;

	/* This is the canonical Pachi arguments parser. You do not strictly
	 * need to decypher it, you can just use it as a boilerplate. */
	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "eqex") && optval) {
				/* eqex takes a required integer argument */
				ctx->eqex = atoi(optval);

			} else if (!strcasecmp(optname, "file") && optval) {
				file = strdup(optval);

			} else if (!strcasecmp(optname, "threshold") && optval) {
				threshold = atof(optval);

			} else if (!strcasecmp(optname, "overrelax") && optval) {
				overrelax = atof(optval);

			} else if (!strcasecmp(optname, "iterations") && optval) {
				iterations = atoi(optval);

			} else {
				fprintf(stderr, "wolf plugin: Invalid argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	/* Initialize the rest of ctx (depending on arguments) here. */
	if (!file) {
		fprintf(stderr, "wolf plugin: file argument not specified\n");
		exit(1);
	}
	ctx->dlh = dlopen(file, RTLD_NOW);
	if (!ctx->dlh) {
		fprintf(stderr, "Cannot load file %s: %s\n", file, dlerror());
		exit(EXIT_FAILURE);
	}
#define loadsym(s_) do {\
	ctx->s_ = dlsym(ctx->dlh, #s_); \
	if (!ctx->s_) { \
		fprintf(stderr, "Cannot find %s in module: %s\n", #s_, dlerror()); \
		exit(EXIT_FAILURE); \
	} \
} while (0)
	loadsym(SETPARAM);
	loadsym(EVALFUN1);
	loadsym(FINDMOVE2);

	ctx->SETPARAM(threshold, overrelax, iterations);

	return ctx;
}

void
pachi_plugin_done(void *data)
{
	struct context *ctx = data;
	dlclose(ctx->dlh);
	free(ctx);
}
