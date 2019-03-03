#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "move.h"
#include "../joseki.h"
#include "playout/moggy.h"
#include "playout/light.h"
#include "engines/montecarlo.h"
#include "playout.h"
#include "timeinfo.h"


/* This is simple monte-carlo engine. It plays MC_GAMES random games from the
 * current board and records win/loss ratio for each first move. The move with
 * the biggest number of winning games gets played. */
/* Note that while the library is based on New Zealand rules, this engine
 * returns moves according to Chinese rules. Thus, it does not return suicide
 * moves. It of course respects positional superko too. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * debug[=DEBUG_LEVEL]		1 is the default; more means more debugging prints
 * games=MC_GAMES		number of random games to play
 * gamelen=MC_GAMELEN		maximal length of played random game
 * playout={light,moggy}[:playout_params]
 */


#define MC_GAMES	40000
#define MC_GAMELEN	400

#define MCDEBUGL(n) DEBUGL_(mc->debug_level, n)


/* Internal engine state. */
typedef struct {
	int debug_level;
	int gamelen;
	floating_t resign_ratio;
	int loss_threshold;
	playout_policy_t *playout;
} montecarlo_t;

/* Per-move playout statistics. */
typedef struct {
	int games;
	int wins;
} move_stat_t;


/* FIXME: Cutoff rule for simulations. Currently we are so fast that this
 * simply does not matter; even 100000 simulations are fast enough to
 * play 5 minutes S.D. on 19x19 and anything more sounds too ridiculous
 * already. */
/* FIXME: We cannot handle seki. Any good ideas are welcome. A possibility is
 * to consider 'pass' among the moves, but this seems tricky. */


static void
board_stats_print(board_t *board, move_stat_t *moves, FILE *f)
{
	int size = board_rsize(board);
	fprintf(f, "\n       ");
	int x, y;
	char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
	for (x = 1; x <= size; x++)
		fprintf(f, "%c    ", asdf[x - 1]);
	fprintf(f, "\n   +-");
	for (x = 1; x <= size; x++)
		fprintf(f, "-----");
	fprintf(f, "+\n");
	for (y = size; y >= 1; y--) {
		fprintf(f, "%2d | ", y);
		for (x = 1; x <= size; x++) {
			coord_t c = coord_xy(x, y);
			if (moves[c].games)
				fprintf(f, "%0.2f ", (floating_t) moves[c].wins / moves[c].games);
			else
				fprintf(f, "---- ");
		}
		fprintf(f, "| ");
		for (x = 1; x <= size; x++)
			fprintf(f, "%4d ", moves[coord_xy(x, y)].games);
		fprintf(f, "|\n");
	}
	fprintf(f, "   +-");
	for (x = 1; x <= size; x++)
		fprintf(f, "-----");
	fprintf(f, "+\n");
}


static coord_t
montecarlo_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	montecarlo_t *mc = (montecarlo_t*)e->data;

	if (ti->dim == TD_WALLTIME) {
		fprintf(stderr, "Warning: TD_WALLTIME time mode not supported, resetting to defaults.\n");
		ti->period = TT_NULL;
	}
	if (ti->period == TT_NULL) {
		ti->period = TT_MOVE;
		ti->dim = TD_GAMES;
		ti->len.games = MC_GAMES;
		ti->len.games_max = 0;
	}
	time_stop_t stop;
	time_stop_conditions(ti, b, 20, 40, 3.0, &stop);

	/* resign when the hope for win vanishes */
	coord_t top_coord = resign;
	floating_t top_ratio = mc->resign_ratio;

	/* We use [0] for pass. Normally, this is an inaccessible corner
	 * of board margin. */
	move_stat_t moves[board_max_coords(b)];
	memset(moves, 0, sizeof(moves));

	int losses = 0;
	int i, superko = 0, good_games = 0;
	for (i = 0; i < stop.desired.playouts; i++) {
		assert(!b->superko_violation);

		board_t b2;
		board_copy(&b2, b);

		coord_t coord;
		board_play_random(&b2, color, &coord, NULL, NULL);
		if (!is_pass(coord) && !group_at(&b2, coord)) {
			/* Multi-stone suicide. We play chinese rules,
			 * so we can't consider this. (Note that we
			 * unfortunately still consider this in playouts.) */
			if (DEBUGL(4)) {
				fprintf(stderr, "SUICIDE DETECTED at %d,%d:\n", coord_x(coord), coord_y(coord));
				board_print(b, stderr);
			}
			continue;
		}

		if (DEBUGL(3))
			fprintf(stderr, "[%d,%d color %d] playing random game\n", coord_x(coord), coord_y(coord), color);

		playout_setup_t ps = playout_setup(mc->gamelen, 0);
		int result = playout_play_game(&ps, &b2, color, NULL, NULL, mc->playout);

		board_done(&b2);

		if (result == 0) {
			/* Superko. We just ignore this playout.
			 * And play again. */
			if (unlikely(superko > 2 * stop.desired.playouts)) {
				/* Uhh. Triple ko, or something? */
				if (MCDEBUGL(0))
					fprintf(stderr, "SUPERKO LOOP. I will pass. Did we hit triple ko?\n");
				goto pass_wins;
			}
			/* This playout didn't count; we should not
			 * disadvantage moves that lead to a superko.
			 * And it is supposed to be rare. */
			i--, superko++;
			continue;
		}

		if (MCDEBUGL(3))
			fprintf(stderr, "\tresult for other player: %d\n", result);

		int pos = is_pass(coord) ? 0 : coord;

		good_games++;
		moves[pos].games++;

		losses += result > 0;
		moves[pos].wins += 1 - (result > 0);

		if (unlikely(!losses && i == mc->loss_threshold)) {
			/* We played out many games and didn't lose once yet.
			 * This game is over. */
			break;
		}
	}

	if (!good_games) {
		/* No moves to try??? */
		if (MCDEBUGL(0)) {
			fprintf(stderr, "OUT OF MOVES! I will pass. But how did this happen?\n");
			board_print(b, stderr);
		}
pass_wins:
		top_coord = pass; top_ratio = 0.5;
		goto move_found;
	}

	foreach_point(b) {
		if (b->moves < 3) {
			/* Simple heuristic: avoid opening too low. Do not
			 * play on second or first line as first white or
			 * first two black moves.*/
			if (coord_x(c) < 3 || coord_x(c) > board_stride(b) - 4
			    || coord_y(c) < 3 || coord_y(c) > board_stride(b) - 4)
				continue;
		}

		floating_t ratio = (floating_t) moves[c].wins / moves[c].games;
		/* Since pass is [0,0], we will pass only when we have nothing
		 * better to do. */
		if (ratio >= top_ratio) {
			top_ratio = ratio;
			top_coord = c == 0 ? pass : c;
		}
	} foreach_point_end;

	if (MCDEBUGL(2)) {
		board_stats_print(b, moves, stderr);
	}

move_found:
	if (MCDEBUGL(1))
		fprintf(stderr, "*** WINNER is %d,%d with score %1.4f (%d games, %d superko)\n", coord_x(top_coord), coord_y(top_coord), top_ratio, i, superko);

	return top_coord;
}

static void
montecarlo_done(engine_t *e)
{
	montecarlo_t *mc = (montecarlo_t*)e->data;
	playout_policy_done(mc->playout);
}

montecarlo_t *
montecarlo_state_init(char *arg, board_t *b)
{
	montecarlo_t *mc = calloc2(1, montecarlo_t);

	mc->debug_level = 1;
	mc->gamelen = MC_GAMELEN;
	joseki_load(board_rsize(b));

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					mc->debug_level = atoi(optval);
				else
					mc->debug_level++;
			} else if (!strcasecmp(optname, "gamelen") && optval) {
				mc->gamelen = atoi(optval);
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "moggy")) {
					mc->playout = playout_moggy_init(playoutarg, b);
				} else if (!strcasecmp(optval, "light")) {
					mc->playout = playout_light_init(playoutarg, b);
				} else {
					fprintf(stderr, "MonteCarlo: Invalid playout policy %s\n", optval);
				}
			} else {
				fprintf(stderr, "MonteCarlo: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	if (!mc->playout)
		mc->playout = playout_light_init(NULL, b);
	mc->playout->debug_level = mc->debug_level;

	mc->resign_ratio = 0.1; /* Resign when most games are lost. */
	mc->loss_threshold = 5000; /* Stop reading if no loss encountered in first 5000 games. */

	return mc;
}


void
engine_montecarlo_init(engine_t *e, char *arg, board_t *b)
{
	montecarlo_t *mc = montecarlo_state_init(arg, b);
	e->name = "MonteCarlo";
	e->comment = "I'm playing in Monte Carlo. When we both pass, I will consider all the stones on the board alive. If you are reading this, write 'yes'. Please bear with me at the game end, I need to fill the whole board; if you help me, we will both be happier. Filling the board will not lose points (NZ rules).";
	e->genmove = montecarlo_genmove;
	e->done = montecarlo_done;
	e->data = mc;
}
