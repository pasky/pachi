#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define DEBUG

#include "board.h"
#include "debug.h"
#include "montecarlo/internal.h"
#include "playout/moggy.h"
#include "random.h"

#define PLDEBUGL(n) DEBUGL_(debug_level, n)


/* Is this ladder breaker friendly for the one who catches ladder. */
static bool
ladder_catcher(struct board *b, int x, int y, enum stone laddered)
{
	enum stone breaker = board_atxy(b, x, y);
	return breaker == stone_other(laddered) || breaker == S_OFFBOARD;
}

static bool
ladder_catches(struct board *b, coord_t coord, group_t laddered)
{
	/* This is very trivial and gets a lot of corner cases wrong.
	 * We need this to be just very fast. */
	//fprintf(stderr, "ladder check\n");

	enum stone lcolor = board_at(b, laddered);

	/* Figure out the ladder direction */
	int x = coord_x(coord, b), y = coord_y(coord, b);
	int xd, yd;
	xd = board_atxy(b, x + 1, y) == S_NONE ? 1 : -1;
	yd = board_atxy(b, x, y + 1) == S_NONE ? 1 : -1;

	/* We do only tight ladders, not loose ladders. Furthermore,
	 * the ladders need to be simple:
	 * . X .             . . X
	 * c O X supported   . c O unsupported
	 * X # #             X O #
	 */

	/* For given (xd,yd), we have two possibilities where to move
	 * next. Consider (-1,1):
	 * n X .   n c X
	 * c O X   X O #
	 * X # #   . X #
	 */
	if (!(ladder_catcher(b, x - xd, y, lcolor) ^ ladder_catcher(b, x, y - yd, lcolor))) {
		/* Silly situation, probably non-simple ladder or suicide. */
		/* TODO: In case of non-simple ladder, play out both variants. */
		if (PLDEBUGL(5))
			fprintf(stderr, "non-simple ladder\n");
		return false;
	}

#define ladder_check(xd_, yd_)	\
	if (board_atxy(b, x, y) != S_NONE) { \
		/* Did we hit a stone when playing out ladder? */ \
		if (ladder_catcher(b, x, y, lcolor)) \
			return true; /* ladder works */ \
		if (board_group_info(b, group_atxy(b, x, y)).lib[0] > 0) \
			return false; /* friend that's not in atari himself */ \
	} else { \
		/* No. So we are at new position. \
		 * We need to check indirect ladder breakers. */ \
		/* . 1 x . \
		 * . x o O <- only at O we can check for o at 1 \
		 * x o o x    otherwise x at O would be still deadly \
		 * o o x . \
		 * We check only for o at 1; x at 1 would mean we \
		 * need to fork (one step earlier). */ \
		if (board_atxy(b, x + (xd_), y + (yd_)) == lcolor) \
			return false; \
	}
#define ladder_horiz	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d horiz step %d\n", x, y, xd); x += xd; /* keima, see above */ ladder_check(-2 * xd, yd); } while (0)
#define ladder_vert	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d vert step %d\n", x, y, yd); y += yd; /* keima, see above */ ladder_check(xd, -2 * yd); } while (0)

	if (ladder_catcher(b, x - xd, y, lcolor))
		ladder_horiz;
	do {
		ladder_vert;
		ladder_horiz;
	} while (1);
}

static coord_t
global_atari_check(struct board *b)
{
	if (b->clen == 0)
		return pass;

	group_t group = b->c[fast_random(b->clen)];
	enum stone color = board_at(b, group);
	coord_t lib = board_group_info(b, group).lib[0];
	if (PLDEBUGL(4))
		fprintf(stderr, "atariiiiiiiii %s of color %d\n", coord2sstr(lib, b), color);
	if (board_at(b, group) == S_OFFBOARD) {
		/* Bogus group. */
		return pass;
	}
	assert(board_at(b, lib) == S_NONE);

	/* Do not suicide... */
	if (!valid_escape_route(b, color, lib))
		return pass;
	if (PLDEBUGL(4))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders. */
	if (ladder_catches(b, lib, group))
		return pass;
	if (PLDEBUGL(4))
		fprintf(stderr, "...no ladder\n");

	return lib;
}

coord_t
playout_moggy(struct montecarlo *mc, struct board *b, enum stone our_real_color)
{
	coord_t c;
	if (PLDEBUGL(4))
		board_print(b, stderr);

	/* Local checks */

	if (!is_pass(b->last_move.coord)) {
		// TODO
	}

	/* Global checks */

	/* Any groups in atari? */
	c = global_atari_check(b);
	if (!is_pass(c))
		return c;

	return pass;
}
