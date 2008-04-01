#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "playout.h"
#include "playout/moggy.h"
#include "random.h"

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)


struct moggy_policy {
	bool ladders;
	int capturerate, patternrate;
	/* These are relative to patternrate. */
	int hanerate, cut1rate, cut2rate;
};

struct move_queue {
	int moves;
	coord_t move[64];
};


static bool
cut1_test_cut(struct playout_policy *p, struct board *b, coord_t base, coord_t cut)
{
	/* X O ?
	 * O(.)?   X is base, (.) is cut
	 * ? ? ? */
	int x = coord_x(base, b), y = coord_y(base, b);
	enum stone color = board_at(b, base);
	int xc = coord_x(cut, b), yc = coord_y(cut, b);

	if (PLDEBUGL(5))
		fprintf(stderr, "Probing CUT1 %s -> %s\n", coord2sstr(base, b), coord2sstr(cut, b));

	/* Kosumi available. Is it cutting? */
	if (board_atxy(b, x, yc) != stone_other(color)
	    || board_atxy(b, xc, y) != stone_other(color))
		return false;

	/* It is! Is the cut protected? */
	enum stone fix1 = board_atxy(b, xc, yc - (y - yc));
	enum stone fix2 = board_atxy(b, xc - (x - xc), yc);
	if (PLDEBUGL(6))
		fprintf(stderr, "Protection check: %d,%d\n", fix1, fix2);
	if (fix1 == stone_other(color) && fix2 == S_NONE)
		return false;
	if (fix2 == stone_other(color) && fix1 == S_NONE)
		return false;

	/* Unprotected cut. Feast! */
	if (PLDEBUGL(6))
		fprintf(stderr, "Passed.\n");
	return true;
}

static void
cut1_test(struct playout_policy *p, struct board *b, struct move *m, struct move_queue *q)
{
	coord_t coord = m->coord;
	int x = coord_x(coord, b), y = coord_y(coord, b);
	enum stone color = board_at(b, coord);

	/* Either last move was cutting threat... */
	foreach_diag_neighbor(b, coord) {
		if (board_at(b, c) != S_NONE)
			continue;
		/* X O ?
		 * O(.)?   X is coord, (.) is c
		 * ? ? ? */
		if (cut1_test_cut(p, b, coord, c))
			q->move[q->moves++] = c;
	} foreach_diag_neighbor_end;

	/* ...or a cuttable hane. */
#define cutbase_test(xb_, yb_) \
		base = coord_xy_otf(xb_, yb_, b); \
		if (board_at(b, base) == stone_other(color)) \
			if (cut1_test_cut(p, b, base, c)) \
				q->move[q->moves++] = c;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != S_NONE)
			continue;
		/* X O ?
		 * O(.)?   O is coord, (.) is c
		 * ? ? ? */
		/* Check for X on both sides of O. */
		int xc = coord_x(c, b);
		int yc = coord_y(c, b);
		coord_t base;
		/* Either x == xc or y == yc. */
		cutbase_test(x - (yc - y), y - (xc - x));
		cutbase_test(x + (yc - y), y + (xc - x));
	});
#undef cutbase_test
}

static bool
cut2_test_cut(struct playout_policy *p, struct board *b, coord_t base, coord_t cut)
{
	/* ? X ?
	 * O(.)O   X is base, (.) is cut
	 * ? ? ? */
	int x = coord_x(base, b), y = coord_y(base, b);
	enum stone color = board_at(b, base);
	int xc = coord_x(cut, b), yc = coord_y(cut, b);

	if (PLDEBUGL(5))
		fprintf(stderr, "Probing CUT2 %s -> %s\n", coord2sstr(base, b), coord2sstr(cut, b));

	/* Nobi available. Is it cutting? */
	if (board_atxy(b, xc - (yc - y), yc - (xc - x)) != stone_other(color)
	    || board_atxy(b, xc + (yc - y), yc + (xc - x)) != stone_other(color))
		return false;

	/* It is! Is the cut protected? */
	enum stone ocolor = stone_other(color);
	if (x == xc) {
		if (PLDEBUGL(6))
			fprintf(stderr, "Protection check - row [%d,%d].\n", xc, yc + (yc - y));
		if (board_atxy(b, xc - 1, yc + (yc - y)) == ocolor
		    || board_atxy(b, xc, yc + (yc - y)) == ocolor
		    || board_atxy(b, xc + 1, yc + (yc - y)) == ocolor)
			return false;
	} else {
		if (PLDEBUGL(6))
			fprintf(stderr, "Protection check - column [%d,%d].\n", xc + (xc - x), yc);
		if (board_atxy(b, xc + (xc - x), yc - 1) == ocolor
		    || board_atxy(b, xc + (xc - x), yc) == ocolor
		    || board_atxy(b, xc + (xc - x), yc + 1) == ocolor)
			return false;
	}

	/* Unprotected cut. Feast! */
	if (PLDEBUGL(6))
		fprintf(stderr, "Passed.\n");
	return true;
}

static void
cut2_test(struct playout_policy *p, struct board *b, struct move *m, struct move_queue *q)
{
	coord_t coord = m->coord;
	int x = coord_x(coord, b), y = coord_y(coord, b);
	enum stone color = board_at(b, coord);

	/* Either last move was cutting threat... */
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != S_NONE)
			continue;
		/* ? X ?
		 * O(.)O   X is coord, (.) is c
		 * ? ? ? */
		if (cut2_test_cut(p, b, coord, c))
			q->move[q->moves++] = c;
	});

	/* ...or a cuttable ikken tobi. */
#define cutbase_test(xb_, yb_) \
		base = coord_xy_otf(xb_, yb_, b); \
		if (board_at(b, base) == stone_other(color)) \
			if (cut2_test_cut(p, b, base, c)) \
				q->move[q->moves++] = c;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != S_NONE)
			continue;
		/* ? X ?
		 * O(.)O   O is coord, (.) is c
		 * ? ? ? */
		/* Check for X on both sides of (.). */
		int xc = coord_x(c, b);
		int yc = coord_y(c, b);
		coord_t base;
		/* Either x == xc or y == yc. */
		cutbase_test(xc - (yc - y), yc - (xc - x));
		cutbase_test(xc + (yc - y), yc + (xc - x));
	});
#undef cutbase_test
}

/* Check if we match a certain pattern centered on given move. */
static coord_t
apply_pattern(struct playout_policy *p, struct board *b, struct move *m, struct move *testmove)
{
	struct moggy_policy *pp = p->data;
	struct move_queue q;
	q.moves = 0;

	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE)
		return pass;

	if (pp->hanerate > fast_random(100)) {
		/* TODO */
	}

	if (pp->cut1rate > fast_random(100)) {
		cut1_test(p, b, m, &q);
	}

	if (pp->cut2rate > fast_random(100)) {
		cut2_test(p, b, m, &q);
	}

	if (PLDEBUGL(4)) {
		fprintf(stderr, "Candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	if (testmove) {
		while (q.moves--)
			if (q.move[q.moves] == testmove->coord) {
				if (PLDEBUGL(4))
					fprintf(stderr, "Found queried move.\n");
				return testmove->coord;
			}
		return pass;
	}

	int i = fast_random(q.moves);
	if (PLDEBUGL(5)) {
		fprintf(stderr, "Chosen %s\n", coord2sstr(q.move[i], b));
	}
	return q.moves ? q.move[i] : pass;
}



/* Is this ladder breaker friendly for the one who catches ladder. */
static bool
ladder_catcher(struct board *b, int x, int y, enum stone laddered)
{
	enum stone breaker = board_atxy(b, x, y);
	return breaker == stone_other(laddered) || breaker == S_OFFBOARD;
}

static bool
ladder_catches(struct playout_policy *p, struct board *b, coord_t coord, group_t laddered)
{
	/* This is very trivial and gets a lot of corner cases wrong.
	 * We need this to be just very fast. */
	//fprintf(stderr, "ladder check\n");

	enum stone lcolor = board_at(b, laddered);

	/* Figure out the ladder direction */
	int x = coord_x(coord, b), y = coord_y(coord, b);
	int xd, yd;
	xd = board_atxy(b, x + 1, y) == S_NONE ? 1 : board_atxy(b, x - 1, y) == S_NONE ? -1 : 0;
	yd = board_atxy(b, x, y + 1) == S_NONE ? 1 : board_atxy(b, x, y - 1) == S_NONE ? -1 : 0;

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
	if (!xd || !yd || !(ladder_catcher(b, x - xd, y, lcolor) ^ ladder_catcher(b, x, y - yd, lcolor))) {
		/* Silly situation, probably non-simple ladder or suicide. */
		/* TODO: In case of basic non-simple ladder, play out both variants. */
		if (PLDEBUGL(5))
			fprintf(stderr, "non-simple ladder\n");
		return false;
	}

#define ladder_check(xd1_, yd1_, xd2_, yd2_)	\
	if (board_atxy(b, x, y) != S_NONE) { \
		/* Did we hit a stone when playing out ladder? */ \
		if (ladder_catcher(b, x, y, lcolor)) \
			return true; /* ladder works */ \
		if (board_group_info(b, group_atxy(b, x, y)).lib[0] > 0) \
			return false; /* friend that's not in atari himself */ \
	} else { \
		/* No. So we are at new position. \
		 * We need to check indirect ladder breakers. */ \
		/* . 2 x . . \
		 * . x o O 1 <- only at O we can check for o at 2 \
		 * x o o x .    otherwise x at O would be still deadly \
		 * o o x . . \
		 * We check for o and x at 1, these are vital. \
		 * We check only for o at 2; x at 2 would mean we \
		 * need to fork (one step earlier). */ \
		enum stone s1 = board_atxy(b, x + (xd1_), y + (yd1_)); \
		if (s1 == lcolor) return false; \
		if (s1 == stone_other(lcolor)) return true; \
		enum stone s2 = board_atxy(b, x + (xd2_), y + (yd2_)); \
		if (s2 == lcolor) return false; \
	}
#define ladder_horiz	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d horiz step %d\n", x, y, xd); x += xd; ladder_check(xd, 0, -2 * xd, yd); } while (0)
#define ladder_vert	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d vert step %d\n", x, y, yd); y += yd; ladder_check(0, yd, xd, -2 * yd); } while (0)

	if (ladder_catcher(b, x - xd, y, lcolor))
		ladder_horiz;
	do {
		ladder_vert;
		ladder_horiz;
	} while (1);
}


static coord_t
group_atari_check(struct playout_policy *p, struct board *b, group_t group)
{
	struct moggy_policy *pp = p->data;
	enum stone color = board_at(b, group);
	coord_t lib = board_group_info(b, group).lib[0];

	if (board_at(b, group) == S_OFFBOARD) {
		/* Bogus group. */
		return pass;
	}
	if (PLDEBUGL(4))
		fprintf(stderr, "atariiiiiiiii %s of color %d\n", coord2sstr(lib, b), color);
	assert(board_at(b, lib) == S_NONE);

	/* Do not suicide... */
	if (!valid_escape_route(b, color, lib))
		return pass;
	if (PLDEBUGL(4))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders. */
	if (pp->ladders && ladder_catches(p, b, lib, group))
		return pass;
	if (PLDEBUGL(4))
		fprintf(stderr, "...no ladder\n");

	return lib;
}

static coord_t
global_atari_check(struct playout_policy *p, struct board *b)
{
	if (b->clen == 0)
		return pass;

	int g_base = fast_random(b->clen);
	for (int g = g_base; g < b->clen; g++) {
		coord_t c = group_atari_check(p, b, b->c[g]);
		if (!is_pass(c))
			return c;
	}
	for (int g = 0; g < g_base; g++) {
		coord_t c = group_atari_check(p, b, b->c[g]);
		if (!is_pass(c))
			return c;
	}
	return pass;
}

coord_t
playout_moggy_choose(struct playout_policy *p, struct board *b, enum stone our_real_color)
{
	struct moggy_policy *pp = p->data;
	coord_t c;

	if (PLDEBUGL(4))
		board_print(b, stderr);

	/* Local checks */
	if (!is_pass(b->last_move.coord)) {
		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			c = apply_pattern(p, b, &b->last_move, NULL);
			if (!is_pass(c))
				return c;
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > fast_random(100)) {
		c = global_atari_check(p, b);
		if (!is_pass(c))
			return c;
	}

	return pass;
}

float
playout_moggy_assess(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;

	if (is_pass(m->coord))
		return NAN;

	if (PLDEBUGL(4))
		board_print(b, stderr);

	/* Pattern check */
	if (pp->patternrate > fast_random(100)) {
		foreach_neighbor(b, m->coord, {
			struct move m2;
			m2.coord = c; m2.color = stone_other(m->color);
			if (apply_pattern(p, b, &m2, m) == m->coord)
				return 1.0;
		});
		foreach_diag_neighbor(b, m->coord) {
			struct move m2;
			m2.coord = c; m2.color = stone_other(m->color);
			if (apply_pattern(p, b, &m2, m) == m->coord)
				return 1.0;
		} foreach_diag_neighbor_end;
	}

	/* Are we dealing with atari? */
	if (pp->capturerate > fast_random(100)) {
		foreach_neighbor(b, m->coord, {
			if (board_group_info(b, group_at(b, c)).libs == 1
			    && group_atari_check(p, b, group_at(b, c)) == m->coord)
				return 1.0;
		});
	}

	return NAN;
}


struct playout_policy *
playout_moggy_init(char *arg)
{
	struct playout_policy *p = calloc(1, sizeof(*p));
	struct moggy_policy *pp = calloc(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_moggy_choose;
	p->assess = playout_moggy_assess;

	pp->capturerate = 100;
	pp->patternrate = 0;
	pp->hanerate = pp->cut1rate = pp->cut2rate = 100;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "ladders")) {
				pp->ladders = true;
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "hanerate") && optval) {
				pp->hanerate = atoi(optval);
			} else if (!strcasecmp(optname, "cut1rate") && optval) {
				pp->cut1rate = atoi(optval);
			} else if (!strcasecmp(optname, "cut2rate") && optval) {
				pp->cut2rate = atoi(optval);
			} else {
				fprintf(stderr, "playout-moggy: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
