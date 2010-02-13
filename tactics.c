#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "tactics.h"


struct selfatari_state {
	int groupcts[S_MAX];
	group_t groupids[S_MAX][4];

	/* This is set if this move puts a group out of _all_
	 * liberties; we need to watch out for snapback then. */
	bool friend_has_no_libs;
	/* We may have one liberty, but be looking for one more.
	 * In that case, @needs_more_lib is id of group
	 * already providing one, don't consider it again. */
	group_t needs_more_lib;
	/* ID of the first liberty, providing it again is not
	 * interesting. */
	coord_t needs_more_lib_except;
};

static int
examine_friendly_groups(struct board *b, enum stone color, coord_t to, struct selfatari_state *s)
{
	for (int i = 0; i < s->groupcts[color]; i++) {
		/* We can escape by connecting to this group if it's
		 * not in atari. */
		group_t g = s->groupids[color][i];

		if (board_group_info(b, g).libs == 1) {
			if (!s->needs_more_lib)
				s->friend_has_no_libs = true;
			// or we already have a friend with 1 lib
			continue;
		}

		/* Could we self-atari the group here? */
		if (board_group_info(b, g).libs > 2)
			return false;

		/* We need to have another liberty, and
		 * it must not be the other liberty of
		 * the group. */
		int lib2 = board_group_info(b, g).lib[0];
		if (lib2 == to) lib2 = board_group_info(b, g).lib[1];
		/* Maybe we already looked at another
		 * group providing one liberty? */
		if (s->needs_more_lib && s->needs_more_lib != g
		    && s->needs_more_lib_except != lib2)
			return false;

		/* Can we get the liberty locally? */
		/* Yes if we are route to more liberties... */
		if (s->groupcts[S_NONE] > 1)
			return false;
		/* ...or one liberty, but not lib2. */
		if (s->groupcts[S_NONE] > 0
		    && !coord_is_adjecent(lib2, to, b))
			return false;

		/* ...ok, then we can still contribute a liberty
		 * later by capturing something. */
		s->needs_more_lib = g;
		s->needs_more_lib_except = lib2;
		s->friend_has_no_libs = false;
	}

	return -1;
}

static int
examine_enemy_groups(struct board *b, enum stone color, coord_t to, struct selfatari_state *s)
{
	/* We may be able to gain a liberty by capturing this group. */
	group_t can_capture = 0;

	/* Examine enemy groups: */
	for (int i = 0; i < s->groupcts[stone_other(color)]; i++) {
		/* We can escape by capturing this group if it's in atari. */
		group_t g = s->groupids[stone_other(color)][i];
		if (board_group_info(b, g).libs > 1)
			continue;

		/* But we need to get to at least two liberties by this;
		 * we already have one outside liberty, or the group is
		 * more than 1 stone (in that case, capturing is always
		 * nice!). */
		if (s->groupcts[S_NONE] > 0 || !group_is_onestone(b, g))
			return false;
		/* ...or, it's a ko stone, */
		if (neighbor_count_at(b, g, color) + neighbor_count_at(b, g, S_OFFBOARD) == 3) {
			/* and we don't have a group to save: then, just taking
			 * single stone means snapback! */
			if (!s->friend_has_no_libs)
				return false;
		}
		/* ...or, we already have one indirect liberty provided
		 * by another group. */
		if (s->needs_more_lib || (can_capture && can_capture != g))
			return false;
		can_capture = g;

	}

	if (DEBUGL(6))
		fprintf(stderr, "no cap group\n");

	if (!s->needs_more_lib && !can_capture && !s->groupcts[S_NONE]) {
		/* We have no hope for more fancy tactics - this move is simply
		 * a suicide, not even a self-atari. */
		if (DEBUGL(6))
			fprintf(stderr, "suicide\n");
		return true;
	}
	/* XXX: I wonder if it makes sense to continue if we actually
	 * just !s->needs_more_lib. */

	return -1;
}

static int
setup_nakade_or_snapback(struct board *b, enum stone color, coord_t to, struct selfatari_state *s)
{
	/* There is another possibility - we can self-atari if it is
	 * a nakade: we put an enemy group in atari from the inside. */
	/* This branch also allows eyes falsification:
	 * O O O . .  (This is different from throw-in to false eye
	 * X X O O .  checked below in that there is no X stone at the
	 * X . X O .  right of the star point in this diagram.)
	 * X X X O O
	 * X O * . . */
	/* TODO: Allow to only nakade if the created shape is dead
	 * (http://senseis.xmp.net/?Nakade). */

	/* This branch also covers snapback, which is kind of special
	 * nakade case. ;-) */
	for (int i = 0; i < s->groupcts[stone_other(color)]; i++) {
		group_t g = s->groupids[stone_other(color)][i];
		if (board_group_info(b, g).libs != 2)
			goto next_group;
		/* Simple check not to re-examine the same group. */
		if (i > 0 && s->groupids[stone_other(color)][i] == s->groupids[stone_other(color)][i - 1])
			continue;

		/* We must make sure the other liberty of that group:
		 * (i) is an internal liberty
		 * (ii) filling it to capture our group will not gain
		 * safety */

		/* Let's look at the other liberty neighbors: */
		int lib2 = board_group_info(b, g).lib[0];
		if (lib2 == to) lib2 = board_group_info(b, g).lib[1];
		foreach_neighbor(b, lib2, {
			/* This neighbor of course does not contribute
			 * anything to the enemy. */
			if (board_at(b, c) == S_OFFBOARD)
				continue;

			/* If the other liberty has empty neighbor,
			 * it must be the original liberty; otherwise,
			 * since the whole group has only 2 liberties,
			 * the other liberty may not be internal and
			 * we are nakade'ing eyeless group from outside,
			 * which is stupid. */
			if (board_at(b, c) == S_NONE) {
				if (c == to)
					continue;
				else
					goto next_group;
			}

			int g2 = group_at(b, c);
			/* If the neighbor is of our color, it must
			 * be our group; if it is a different group,
			 * it must not be in atari. */
			/* X X X X  We will not allow play on 'a',
			 * X X a X  because 'b' would capture two
			 * X O b X  different groups, forming two
			 * X X X X  eyes. */
			if (board_at(b, c) == color) {
				if (board_group_info(b, group_at(b, c)).libs > 1)
					continue;
				/* Our group == one of the groups
				 * we (@to) are connected to. */
				int j;
				for (j = 0; j < 4; j++)
					if (s->groupids[color][j] == g2)
						break;
				if (j == 4)
					goto next_group;
				continue;
			}

			/* The neighbor is enemy color. It's ok if
			 * it's still the same group or this is its
			 * only liberty. */
			if (g == g2 || board_group_info(b, g2).libs == 1)
				continue;
			/* Otherwise, it must have the exact same
			 * liberties as the original enemy group. */
			if (board_group_info(b, g2).libs == 2
			    && (board_group_info(b, g2).lib[0] == to
			        || board_group_info(b, g2).lib[1] == to))
				continue;

			goto next_group;
		});

		/* Now, we must distinguish between nakade and eye
		 * falsification; we must not falsify an eye by more
		 * than two stones. */
		if (s->groupcts[color] < 1 ||
		    (s->groupcts[color] == 1 && group_is_onestone(b, s->groupids[color][0])))
			return false;

		/* We would create more than 2-stone group; in that
		 * case, the liberty of our result must be lib2,
		 * indicating this really is a nakade. */
		for (int j = 0; j < s->groupcts[color]; j++) {
			group_t g2 = s->groupids[color][j];
			assert(board_group_info(b, g2).libs <= 2);
			if (board_group_info(b, g2).libs == 2) {
				if (board_group_info(b, g2).lib[0] != lib2
				    && board_group_info(b, g2).lib[1] != lib2)
					goto next_group;
			} else {
				assert(board_group_info(b, g2).lib[0] == to);
			}
		}

		return false;
next_group:	
		/* Unless we are dealing with snapback setup, we don't need to look
		 * further. */
		if (!s->groupcts[color])
			return -1;
	}

	return -1;
}

static int
check_throwin(struct board *b, enum stone color, coord_t to, struct selfatari_state *s)
{
	/* We can be throwing-in to false eye:
	 * X X X O X X X O X X X X X
	 * X . * X * O . X * O O . X
	 * # # # # # # # # # # # # # */
	/* We cannot sensibly throw-in into a corner. */
	if (neighbor_count_at(b, to, S_OFFBOARD) < 2
	    && neighbor_count_at(b, to, stone_other(color))
	       + neighbor_count_at(b, to, S_OFFBOARD) == 3
	    && board_is_false_eyelike(b, &to, stone_other(color))) {
		assert(s->groupcts[color] <= 1);
		/* Single-stone throw-in may be ok... */
		if (s->groupcts[color] == 0) {
			/* O X .  There is one problem - when it's
			 * . * X  actually not a throw-in!
			 * # # #  */
			foreach_neighbor(b, to, {
				if (board_at(b, c) == S_NONE) {
					/* Is the empty neighbor an escape path? */
					/* (Note that one S_NONE neighbor is already @to.) */
					if (neighbor_count_at(b, c, stone_other(color))
					    + neighbor_count_at(b, c, S_OFFBOARD) < 2)
						return -1;
				}
			});
			return false;
		}

		/* Multi-stone throwin...? */
		assert(s->groupcts[color] == 1);
		group_t g = s->groupids[color][0];

		assert(board_group_info(b, g).libs <= 2);
		/* Suicide is definitely NOT ok, no matter what else
		 * we could test. */
		if (board_group_info(b, g).libs == 1)
			return true;

		/* In that case, we must be connected to at most one stone,
		 * or throwin will not destroy any eyes. */
		if (group_is_onestone(b, g))
			return false;
	}
	return -1;
}

bool
is_bad_selfatari_slow(struct board *b, enum stone color, coord_t to)
{
	if (DEBUGL(5))
		fprintf(stderr, "sar check %s %s\n", stone2str(color), coord2sstr(to, b));
	/* Assess if we actually gain any liberties by this escape route.
	 * Note that this is not 100% as we cannot check whether we are
	 * connecting out or just to ourselves. */

	struct selfatari_state s;
	memset(&s, 0, sizeof(s));
	int d;

	foreach_neighbor(b, to, {
		enum stone color = board_at(b, c);
		s.groupids[color][s.groupcts[color]++] = group_at(b, c);
	});

	/* We have shortage of liberties; that's the point. */
	assert(s.groupcts[S_NONE] <= 1);

	d = examine_friendly_groups(b, color, to, &s);
	if (d >= 0)
		return d;

	if (DEBUGL(6))
		fprintf(stderr, "no friendly group\n");

	d = examine_enemy_groups(b, color, to, &s);
	if (d >= 0)
		return d;

	if (DEBUGL(6))
		fprintf(stderr, "no escape\n");

	d = setup_nakade_or_snapback(b, color, to, &s);
	if (d >= 0)
		return d;

	if (DEBUGL(6))
		fprintf(stderr, "no nakade group\n");

	d = check_throwin(b, color, to, &s);
	if (d >= 0)
		return d;

	if (DEBUGL(6))
		fprintf(stderr, "no throw-in group\n");

	/* No way to pull out, no way to connect out. This really
	 * is a bad self-atari! */
	return true;
}


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
		/* Then, can X actually "play" 1 in the ladder? */ \
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
board_stone_radar(struct board *b, coord_t coord, int distance)
{
	int bounds[4] = {
		coord_x(coord, b) - distance,
		coord_y(coord, b) - distance,
		coord_x(coord, b) + distance,
		coord_y(coord, b) + distance
	};
	for (int i = 0; i < 4; i++)
		if (bounds[i] < 1)
			bounds[i] = 1;
		else if (bounds[i] > board_size(b) - 2)
			bounds[i] = board_size(b) - 2;
	for (int x = bounds[0]; x <= bounds[2]; x++)
		for (int y = bounds[1]; y <= bounds[3]; y++)
			if (board_atxy(b, x, y) != S_NONE) {
				/* fprintf(stderr, "radar %d,%d,%d: %d,%d (%d)\n",
					coord_x(coord, b), coord_y(coord, b),
					distance, x, y, board_atxy(b, x, y)); */
				return true;
			}
	return false;
}


void
cfg_distances(struct board *b, coord_t start, int *distances, int maxdist)
{
	/* Queue for d+1 spots; no two spots of the same group
	 * should appear in the queue. */
#define qinc(x) (x = ((x + 1) >= board_size2(b) ? ((x) + 1 - board_size2(b)) : (x) + 1))
	coord_t queue[board_size2(b)]; int qstart = 0, qstop = 0;

	foreach_point(b) {
		distances[c] = board_at(b, c) == S_OFFBOARD ? maxdist + 1 : -1;
	} foreach_point_end;

	queue[qstop++] = start;
	for (int d = 0; d <= maxdist; d++) {
		/* Process queued moves, while setting the queue
		 * for new wave. */
		int qa = qstart, qb = qstop;
		qstart = qstop;
		for (int q = qa; q < qb; qinc(q)) {
#define cfg_one(coord, grp) do {\
	distances[coord] = d; \
	foreach_neighbor (b, coord, { \
		if (distances[c] < 0 && (!grp || group_at(b, coord) != grp)) { \
			queue[qstop] = c; \
			qinc(qstop); \
		} \
	}); \
} while (0)
			coord_t cq = queue[q];
			if (distances[cq] >= 0)
				continue; /* We already looked here. */
			if (board_at(b, cq) == S_NONE) {
				cfg_one(cq, 0);
			} else {
				group_t g = group_at(b, cq);
				foreach_in_group(b, g) {
					cfg_one(c, g);
				} foreach_in_group_end;
			}
#undef cfg_one
		}
	}

	foreach_point(b) {
		if (distances[c] < 0)
			distances[c] = maxdist + 1;
	} foreach_point_end;
}


float
board_effective_handicap(struct board *b, int first_move_value)
{
	assert(b->handicap != 1);
	return (b->handicap ? b->handicap : 1) * first_move_value + 0.5 - b->komi;
}


bool
pass_is_safe(struct board *b, enum stone color, struct move_queue *mq)
{
	float score = board_official_score(b, mq);
	if (color == S_BLACK)
		score = -score;
	//fprintf(stderr, "%d score %f\n", color, score);
	return (score > 0);
}


/* On average 25% of points remain empty at the end of a game */
#define EXPECTED_FINAL_EMPTY_PERCENT 25

/* Returns estimated number of remaining moves for one player until end of game. */
int
board_estimated_moves_left(struct board *b)
{
	int total_points = (board_size(b)-2)*(board_size(b)-2);
	int moves_left = (b->flen - total_points*EXPECTED_FINAL_EMPTY_PERCENT/100)/2;
	return moves_left > MIN_MOVES_LEFT ? moves_left : MIN_MOVES_LEFT;
}
