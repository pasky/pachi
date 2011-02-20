#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "tactics/ladder.h"


/* Is this ladder breaker friendly for the one who catches ladder. */
static bool
ladder_catcher(struct board *b, int x, int y, enum stone laddered)
{
	enum stone breaker = board_atxy(b, x, y);
	return breaker == stone_other(laddered) || breaker == S_OFFBOARD;
}

bool
is_border_ladder(struct board *b, coord_t coord, enum stone lcolor)
{
	int x = coord_x(coord, b), y = coord_y(coord, b);

	if (DEBUGL(5))
		fprintf(stderr, "border ladder\n");
	/* Direction along border; xd is horiz. border, yd vertical. */
	int xd = 0, yd = 0;
	if (board_atxy(b, x + 1, y) == S_OFFBOARD || board_atxy(b, x - 1, y) == S_OFFBOARD)
		yd = 1;
	else
		xd = 1;
	/* Direction from the border; -1 is above/left, 1 is below/right. */
	int dd = (board_atxy(b, x + yd, y + xd) == S_OFFBOARD) ? 1 : -1;
	if (DEBUGL(6))
		fprintf(stderr, "xd %d yd %d dd %d\n", xd, yd, dd);
	/* | ? ?
	 * | . O #
	 * | c X #
	 * | . O #
	 * | ? ?   */
	/* This is normally caught, unless we have friends both above
	 * and below... */
	if (board_atxy(b, x + xd * 2, y + yd * 2) == lcolor
	    && board_atxy(b, x - xd * 2, y - yd * 2) == lcolor)
		return false;
	/* ...or can't block where we need because of shortage
	 * of liberties. */
	int libs1 = board_group_info(b, group_atxy(b, x + xd - yd * dd, y + yd - xd * dd)).libs;
	int libs2 = board_group_info(b, group_atxy(b, x - xd - yd * dd, y - yd - xd * dd)).libs;
	if (DEBUGL(6))
		fprintf(stderr, "libs1 %d libs2 %d\n", libs1, libs2);
	if (libs1 < 2 && libs2 < 2)
		return false;
	if (board_atxy(b, x + xd * 2, y + yd * 2) == lcolor && libs1 < 3)
		return false;
	if (board_atxy(b, x - xd * 2, y - yd * 2) == lcolor && libs2 < 3)
		return false;
	return true;
}


/* This is very trivial and gets a lot of corner cases wrong.
 * We need this to be just very fast. One important point is
 * that we sometimes might not notice a ladder but if we do,
 * it should always work; thus we can use this for strong
 * negative hinting safely. */

static bool
middle_ladder_walk(struct board *b, enum stone lcolor, int x, int y, int xd, int yd)
{
#define ladder_check(xd1_, yd1_, xd2_, yd2_, xd3_, yd3_)	\
	if (board_atxy(b, x, y) != S_NONE) { \
		/* Did we hit a stone when playing out ladder? */ \
		if (ladder_catcher(b, x, y, lcolor)) \
			return true; /* ladder works */ \
		if (board_group_info(b, group_atxy(b, x, y)).lib[0] > 0) \
			return false; /* friend that's not in atari himself */ \
	} else { \
		/* No. So we are at new position. \
		 * We need to check indirect ladder breakers. */ \
		/* . 2 x 3 . \
		 * . x o O 1 <- only at O we can check for o at 2 \
		 * x o o x .    otherwise x at O would be still deadly \
		 * o o x . . \
		 * We check for o and x at 1, these are vital. \
		 * We check only for o at 2; x at 2 would mean we \
		 * need to fork (one step earlier). */ \
		coord_t c = coord_xy(b, x, y); \
		coord_t c1 = coord_xy(b, x + (xd1_), y + (yd1_)); \
		enum stone s1 = board_at(b, c1); \
		if (s1 == lcolor) return false; \
		if (s1 == stone_other(lcolor)) { \
			/* One more thing - if the position at 3 is \
			 * friendly and safe, we escaped anyway! */ \
			coord_t c3 = coord_xy(b, x + (xd3_), y + (yd3_)); \
			return board_at(b, c3) != lcolor \
			       || board_group_info(b, group_at(b, c3)).libs < 2; \
		} \
		enum stone s2 = board_atxy(b, x + (xd2_), y + (yd2_)); \
		if (s2 == lcolor) return false; \
		if (neighbor_count_at(b, c1, lcolor) + neighbor_count_at(b, c1, S_OFFBOARD) >= 2) \
			return false; /* It would be self-atari! */ \
	}
#define ladder_horiz	do { if (DEBUGL(6)) fprintf(stderr, "%d,%d horiz step (%d,%d)\n", x, y, xd, yd); x += xd; ladder_check(xd, 0, -2 * xd, yd, 0, yd); } while (0)
#define ladder_vert	do { if (DEBUGL(6)) fprintf(stderr, "%d,%d vert step of (%d,%d)\n", x, y, xd, yd); y += yd; ladder_check(0, yd, xd, -2 * yd, xd, 0); } while (0)

	if (ladder_catcher(b, x - xd, y, lcolor))
		ladder_horiz;
	do {
		ladder_vert;
		ladder_horiz;
	} while (1);
}

bool
is_middle_ladder(struct board *b, coord_t coord, enum stone lcolor)
{
	int x = coord_x(coord, b), y = coord_y(coord, b);

	/* Figure out the ladder direction */
	int xd, yd;
	xd = board_atxy(b, x + 1, y) == S_NONE ? 1 : board_atxy(b, x - 1, y) == S_NONE ? -1 : 0;
	yd = board_atxy(b, x, y + 1) == S_NONE ? 1 : board_atxy(b, x, y - 1) == S_NONE ? -1 : 0;

	if (!xd || !yd) {
		if (DEBUGL(5))
			fprintf(stderr, "no ladder, too little space; self-atari?\n");
		return false;
	}

	/* For given (xd,yd), we have two possibilities where to move
	 * next. Consider (-1,-1):
	 * n X .   n c X
	 * c O X   X O #
	 * X # #   . X #
	 */
	bool horiz_first = ladder_catcher(b, x, y - yd, lcolor); // left case
	bool vert_first = ladder_catcher(b, x - xd, y, lcolor); // right case

	/* We don't have to look at the other 'X' in the position - if it
	 * wouldn't be there, the group wouldn't be in atari. */

	/* We do only tight ladders, not loose ladders. Furthermore,
	 * the ladders need to be simple:
	 * . X .             . . X
	 * c O X supported   . c O unsupported
	 * X # #             X O #
	 */
	assert(!(horiz_first && vert_first));
	if (!horiz_first && !vert_first) {
		/* TODO: In case of basic non-simple ladder, play out both variants. */
		if (DEBUGL(5))
			fprintf(stderr, "non-simple ladder\n");
		return false;
	}

	/* We do that below for further moves, but now initially - check
	 * that at 'c', we aren't putting any of the catching stones
	 * in atari. */
#if 1 // this might be broken?
#define check_catcher_danger(b, x_, y_) do { \
	if (board_atxy(b, (x_), (y_)) != S_OFFBOARD \
	    && board_group_info(b, group_atxy(b, (x_), (y_))).libs <= 2) { \
		if (DEBUGL(5)) \
			fprintf(stderr, "ladder failed - atari at the beginning\n"); \
		return false; \
	} } while (0)

	if (horiz_first) {
		check_catcher_danger(b, x, y - yd);
		check_catcher_danger(b, x - xd, y + yd);
	} else {
		check_catcher_danger(b, x - xd, y);
		check_catcher_danger(b, x + xd, y - yd);
	}
#undef check_catcher_danger
#endif
	
	return middle_ladder_walk(b, lcolor, x, y, xd, yd);
}
