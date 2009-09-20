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
	bool ladders, ladderassess, borderladders;
	int lcapturerate, capturerate, patternrate;
	/* These are relative to patternrate. */
	int hanerate, cut1rate, cut2rate;
};

#define MQL 64
struct move_queue {
	int moves;
	coord_t move[MQL];
};


/* Pattern encoding:
 * X: black;  O: white;  .: empty;  #: edge
 * x: !black; o: !white; ?: any
 *
 * extra X: pattern valid only for one side;
 * middle point ignored. */

static char mogo_patterns_src[][11] = {
	/* hane pattern - enclosing hane */
	"XOX"
	"..."
	"???",
	/* hane pattern - non-cutting hane */
	"XO."
	"..."
	"?.?",
	/* hane pattern - magari */
	"XO?"
	"X.."
	"?.?",
	/* hane pattern - thin hane (SUSPICIOUS) */
	"XOO"
	"..."
	"?.?" "X",
	/* cut1 pattern (kiri) - unprotected cut */
	"XO?"
	"O.o"
	"?o?",
	/* cut1 pattern (kiri) - peeped cut */
	"XO?"
	"O.X"
	"???",
	/* cut2 pattern (de) */
	"?X?"
	"O.O"
	"ooo",
	/* side pattern - chase */
	"X.?"
	"O.?"
	"##?",
	/* side pattern - weirdness (SUSPICIOUS) */
	"?X?"
	"X.O"
	"###",
	/* side pattern - sagari (SUSPICIOUS) */
	"?XO"
	"?.?" /* "?.x" ? */
	"###" "X",
	/* side pattern - weirdcut (SUSPICIOUS) */
	"?OX"
	"?.O"
	"?##" "X",
	/* side pattern - cut (SUSPICIOUS) */
	"?OX"
	"X.O"
	"###" "X" /* no "X"? */,
};
#define mogo_patterns_src_n sizeof(mogo_patterns_src) / sizeof(mogo_patterns_src[0])

/* Hashtable: 2*8 bits (ignore middle point, 2 bits per intersection) */
/* Value: 0: no pattern, 1: black pattern, 2: white pattern, 3: both patterns */
static char mogo_patterns[65536];

static void
_record_pattern(char *table, int pat, int fixed_color)
{
	/* Original color assignment */
	table[pat] = fixed_color ? fixed_color : 3;

	/* Reverse color assignment - achieved by swapping odd and even bits */
	pat = ((pat >> 1) & 0x5555) | ((pat & 0x5555) << 1);
	table[pat] = fixed_color ? 2 - (fixed_color == 2) : 3;
}

static void
_gen_pattern(char *table, int pat, char *src, int srclen, int fixed_color)
{
	for (; srclen > 0; src++, srclen--) {
		if (srclen == 5)
			continue;
		int patofs = (srclen > 5 ? srclen - 1 : srclen) - 1;
		switch (*src) {
			case '?':
				*src = '.'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = 'X'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = 'O'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = '#'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = '?'; // for future recursions
				return;
			case 'x':
				*src = '.'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = 'O'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = '#'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = 'x'; // for future recursions
				return;
			case 'o':
				*src = '.'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = 'X'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = '#'; _gen_pattern(table, pat, src, srclen, fixed_color);
				*src = 'o'; // for future recursions
				return;
			case '.': /* 0 */ break;
			case 'X': pat |= S_BLACK << (patofs * 2); break;
			case 'O': pat |= S_WHITE << (patofs * 2); break;
			case '#': pat |= S_OFFBOARD << (patofs * 2); break;
		}
	}

	/* Original pattern */
	//fprintf(stderr, "[%s] %04x\n", src - 9, pat);
	_record_pattern(table, pat, fixed_color);
	/* V/H mirror pattern; reverse order of all 2-bit values */
	{
		int p2 = pat;
		p2 = ((p2 >> 2) & 0x3333) | ((p2 & 0x3333) << 2);
		p2 = ((p2 >> 4) & 0x0F0F) | ((p2 & 0x0F0F) << 4);
		p2 = ((p2 >> 8) & 0x00FF) | ((p2 & 0x00FF) << 8);
		//fprintf(stderr, "[%s] %04x\n", src - 9, p2);
		_record_pattern(table, p2, fixed_color);
	}
	/* V mirror pattern; reverse order of 3-2-3 chunks */
	{
		int p2 = ((pat & 0xfc00) >> 10) | (pat & 0x03c0) | ((pat & 0x003f) << 10);
		fprintf(stderr, "[%s] %04x\n", src - 9, p2);
		_record_pattern(table, p2, fixed_color);
		/* H mirror pattern; reverse this bitstring */
		p2 = ((p2 >> 2) & 0x3333) | ((p2 & 0x3333) << 2);
		p2 = ((p2 >> 4) & 0x0F0F) | ((p2 & 0x0F0F) << 4);
		p2 = ((p2 >> 8) & 0x00FF) | ((p2 & 0x00FF) << 8);
		//fprintf(stderr, "[%s] %04x\n", src - 9, p2);
		_record_pattern(table, p2, fixed_color);
	}
}

static void __attribute__((constructor))
_init_patterns(void)
{
	for (int i = 0; i < mogo_patterns_src_n; i++) {
		int fixed_color = 0;
		switch (mogo_patterns_src[i][9]) {
			case 'X': fixed_color = S_BLACK; break;
			case 'O': fixed_color = S_WHITE; break;
		}
		_gen_pattern(mogo_patterns, 0, mogo_patterns_src[i], 9, fixed_color);
	}
}



/* Check if we match any pattern centered on given move. */
static bool
test_pattern_here(struct playout_policy *p, char *hashtable,
		struct board *b, struct move *m)
{
	int pat = 0;
	int x = coord_x(m->coord, b), y = coord_y(m->coord, b);
	pat |= (board_atxy(b, x - 1, y - 1) << 14)
		| (board_atxy(b, x, y - 1) << 12)
		| (board_atxy(b, x + 1, y - 1) << 10);
	pat |= (board_atxy(b, x - 1, y) << 8)
		| (board_atxy(b, x + 1, y) << 6);
	pat |= (board_atxy(b, x - 1, y + 1) << 4)
		| (board_atxy(b, x, y + 1) << 2)
		| (board_atxy(b, x + 1, y + 1));
	//fprintf(stderr, "(%d,%d) hashtable[%04x] = %d\n", x, y, pat, hashtable[pat]);
	return (hashtable[pat] & m->color);
}

static void
apply_pattern_here(struct playout_policy *p, char *hashtable,
		struct board *b, struct move *m, struct move_queue *q)
{
	if (test_pattern_here(p, hashtable, b, m))
		q->move[q->moves++] = m->coord;
}

/* Check if we match any pattern around given move (with the other color to play). */
static coord_t
apply_pattern(struct playout_policy *p, struct board *b, struct move *m, struct move *testmove)
{
	//struct moggy_policy *pp = p->data;
	struct move_queue q;
	q.moves = 0;

	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return pass;

	// FIXME: Fix assess callers
	foreach_neighbor(b, m->coord, {
		struct move m2; m2.coord = c; m2.color = stone_other(m->color);
		if (board_at(b, c) == S_NONE)
			apply_pattern_here(p, mogo_patterns, b, &m2, &q);
	});
	foreach_diag_neighbor(b, m->coord) {
		struct move m2; m2.coord = c; m2.color = stone_other(m->color);
		if (board_at(b, c) == S_NONE)
			apply_pattern_here(p, mogo_patterns, b, &m2, &q);
	} foreach_diag_neighbor_end;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Pattern candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	if (testmove) {
		while (q.moves--)
			if (q.move[q.moves] == testmove->coord) {
				if (PLDEBUGL(5))
					fprintf(stderr, "Found queried move.\n");
				return testmove->coord;
			}
		return pass;
	}

	int i = fast_random(q.moves);
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
	struct moggy_policy *pp = p->data;

	/* This is very trivial and gets a lot of corner cases wrong.
	 * We need this to be just very fast. One important point is
	 * that we sometimes might not notice a ladder but if we do,
	 * it should always work; thus we can use this for strong
	 * negative hinting safely. */
	//fprintf(stderr, "ladder check\n");

	enum stone lcolor = board_at(b, group_base(laddered));
	int x = coord_x(coord, b), y = coord_y(coord, b);

	/* First, special-case first-line "ladders". This is a huge chunk
	 * of ladders we actually meet and want to play. */
	if (pp->borderladders
	    && neighbor_count_at(b, coord, S_OFFBOARD) == 1
	    && neighbor_count_at(b, coord, lcolor) == 1) {
		if (PLDEBUGL(5))
			fprintf(stderr, "border ladder\n");
		/* Direction along border; xd is horiz. border, yd vertical. */
		int xd = 0, yd = 0;
		if (board_atxy(b, x + 1, y) == S_OFFBOARD || board_atxy(b, x - 1, y) == S_OFFBOARD)
			yd = 1;
		else
			xd = 1;
		/* Direction from the border; -1 is above/left, 1 is below/right. */
		int dd = (board_atxy(b, x + yd, y + xd) == S_OFFBOARD) ? 1 : -1;
		if (PLDEBUGL(6))
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
		if (PLDEBUGL(6))
			fprintf(stderr, "libs1 %d libs2 %d\n", libs1, libs2);
		if (libs1 < 2 && libs2 < 2)
			return false;
		if (board_atxy(b, x + xd * 2, y + yd * 2) == lcolor && libs1 < 3)
			return false;
		if (board_atxy(b, x - xd * 2, y - yd * 2) == lcolor && libs2 < 3)
			return false;
		return true;
	}

	/* Figure out the ladder direction */
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
	enum stone color = board_at(b, group_base(group));
	coord_t lib = board_group_info(b, group).lib[0];

	assert(color != S_OFFBOARD && color != S_NONE);
	if (PLDEBUGL(5))
		fprintf(stderr, "atariiiiiiiii %s of color %d\n", coord2sstr(lib, b), color);
	assert(board_at(b, lib) == S_NONE);

	/* Do not suicide... */
	if (!valid_escape_route(b, color, lib))
		goto caught;
	if (PLDEBUGL(6))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders. */
	if (pp->ladders && ladder_catches(p, b, lib, group)) {
		goto caught;
	}
	if (PLDEBUGL(6))
		fprintf(stderr, "...no ladder\n");

	return lib;

caught:
	/* There is still hope - can't we capture some neighbor? */
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != stone_other(color)
			    || board_group_info(b, group_at(b, c)).libs > 1)
				continue;
			if (PLDEBUGL(6))
				fprintf(stderr, "can capture group %d\n", group_at(b, c));
			/* If we are saving our group, capture! */
			if (b->last_move.color == stone_other(color))
				return board_group_info(b, group_at(b, c)).lib[0];
			/* If we chase the group, capture it now! */
			return lib;
		});
	} foreach_in_group_end;
	return pass;
}

static coord_t
global_atari_check(struct playout_policy *p, struct board *b)
{
	if (b->clen == 0)
		return pass;

	int g_base = fast_random(b->clen);
	for (int g = g_base; g < b->clen; g++) {
		coord_t c = group_atari_check(p, b, group_at(b, group_base(b->c[g])));
		if (!is_pass(c))
			return c;
	}
	for (int g = 0; g < g_base; g++) {
		coord_t c = group_atari_check(p, b, group_at(b, group_base(b->c[g])));
		if (!is_pass(c))
			return c;
	}
	return pass;
}

static coord_t
local_atari_check(struct playout_policy *p, struct board *b, struct move *m, struct move *testmove)
{
	struct move_queue q;
	q.moves = 0;

	/* Did the opponent play a self-atari? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 1) {
		coord_t l = group_atari_check(p, b, group_at(b, m->coord));
		if (!is_pass(l))
			q.move[q.moves++] = l;
	}

	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;
		coord_t l = group_atari_check(p, b, g);
		if (!is_pass(l))
			q.move[q.moves++] = l;
	});

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Local atari candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	if (testmove) {
		while (q.moves--)
			if (q.move[q.moves] == testmove->coord) {
				if (PLDEBUGL(5))
					fprintf(stderr, "Found queried move.\n");
				return testmove->coord;
			}
		return pass;
	}

	int i = fast_random(q.moves);
	return q.moves ? q.move[i] : pass;
}

coord_t
playout_moggy_choose(struct playout_policy *p, struct board *b, enum stone our_real_color)
{
	struct moggy_policy *pp = p->data;
	coord_t c;

	if (PLDEBUGL(5))
		board_print(b, stderr);

	/* Local checks */
	if (!is_pass(b->last_move.coord)) {
		/* Local group in atari? */
		if (pp->lcapturerate > fast_random(100)) {
			c = local_atari_check(p, b, &b->last_move, NULL);
			if (!is_pass(c))
				return c;
		}

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

	if (PLDEBUGL(5)) {
		fprintf(stderr, "ASSESS of %s:\n", coord2sstr(m->coord, b));
		board_print(b, stderr);
	}

	/* Are we dealing with atari? */
	if (pp->lcapturerate > fast_random(100)) {
		foreach_neighbor(b, m->coord, {
			struct move m2;
			m2.coord = c; m2.color = stone_other(m->color);
			if (local_atari_check(p, b, &m2, m) == m->coord)
				return 1.0;
		});

		/* Assess ladders anywhere, local or not. */
		if (pp->ladderassess) {
			//fprintf(stderr, "ASSESS %s\n", coord2sstr(m->coord, b));
			foreach_neighbor(b, m->coord, {
				if (board_at(b, c) == S_NONE || board_at(b, c) == S_OFFBOARD)
					continue;
				group_t g = group_at(b, c);
				if (board_group_info(b, g).libs != 1)
					continue;
				if (ladder_catches(p, b, m->coord, g))
					return 0.0;
			});
		}
	}

	/* Pattern check */
	if (pp->patternrate > fast_random(100)) {
		if (test_pattern_here(p, mogo_patterns, b, m))
			return 1.0;
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

	pp->lcapturerate = 75;
	pp->capturerate = 75;
	pp->patternrate = 75;
	pp->hanerate = pp->cut1rate = pp->cut2rate = 75;
	pp->ladders = pp->borderladders = true;
	pp->ladderassess = true;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "lcapturerate") && optval) {
				pp->lcapturerate = atoi(optval);
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "hanerate") && optval) {
				pp->hanerate = atoi(optval);
			} else if (!strcasecmp(optname, "cut1rate") && optval) {
				pp->cut1rate = atoi(optval);
			} else if (!strcasecmp(optname, "cut2rate") && optval) {
				pp->cut2rate = atoi(optval);
			} else if (!strcasecmp(optname, "ladders")) {
				pp->ladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "borderladders")) {
				pp->borderladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "ladderassess")) {
				pp->ladderassess = optval && *optval == '0' ? false : true;
			} else {
				fprintf(stderr, "playout-moggy: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
