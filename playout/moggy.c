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


/* Note that the context can be shared by multiple threads! */

struct moggy_policy {
	bool ladders, ladderassess, borderladders, assess_local;
	int lcapturerate, capturerate, patternrate;
	int selfatarirate;

	/* Hashtable: 2*8 bits (ignore middle point, 2 bits per intersection) */
	/* Value: 0: no pattern, 1: black pattern,
	 * 2: white pattern, 3: both patterns */
	char patterns[65536];
};

#define MQL 64
struct move_queue {
	int moves;
	coord_t move[MQL];
};

static void
mq_nodup(struct move_queue *q)
{
	if ((q->moves > 1 && q->move[q->moves - 2] == q->move[q->moves - 1])
	    || (q->moves > 2 && q->move[q->moves - 3] == q->move[q->moves - 1])
	    || (q->moves > 3 && q->move[q->moves - 4] == q->move[q->moves - 1]))
		q->moves--;
}


/* Pattern encoding:
 * X: black;  O: white;  .: empty;  #: edge
 * x: !black; o: !white; ?: any
 *
 * extra X: pattern valid only for one side;
 * middle point ignored. */

static char moggy_patterns_src[][11] = {
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
	"x.?",
	/* hane pattern - thin hane */
	"XOO"
	"..."
	"?.?" "X",
	/* generic pattern - katatsuke or diagonal attachment; similar to magari */
	".O."
	"X.."
	"...",
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
	/* cut keima (not in Mogo) */
	"OX?"
	"o.O"
	"o??",
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
	"x.x" /* Mogo has "x.?" */
	"###" /* Mogo has "X" */,
	/* side pattern - throw-in (SUSPICIOUS) */
#if 0
	"?OX"
	"o.O"
	"?##" "X",
#endif
	/* side pattern - cut (SUSPICIOUS) */
	"?OX"
	"X.O"
	"###" /* Mogo has "X" */,
};
#define moggy_patterns_src_n sizeof(moggy_patterns_src) / sizeof(moggy_patterns_src[0])

static void
pattern_record(char *table, char *str, int pat, int fixed_color)
{
	/* Original color assignment */
	table[pat] = fixed_color ? fixed_color : 3;
	//fprintf(stderr, "[%s] %04x %d\n", str, pat, fixed_color);

	/* Reverse color assignment - achieved by swapping odd and even bits */
	pat = ((pat >> 1) & 0x5555) | ((pat & 0x5555) << 1);
	table[pat] = fixed_color ? 2 - (fixed_color == 2) : 3;
	//fprintf(stderr, "[%s] %04x %d\n", str, pat, fixed_color);
}

static int
pat_vmirror(int pat)
{
	/* V mirror pattern; reverse order of 3-2-3 chunks */
	return ((pat & 0xfc00) >> 10) | (pat & 0x03c0) | ((pat & 0x003f) << 10);
}

static int
pat_hmirror(int pat)
{
	/* H mirror pattern; reverse order of 2-bit values within the chunks */
#define rev3(p) ((p >> 4) | (p & 0xc) | ((p & 0x3) << 4))
#define rev2(p) ((p >> 2) | ((p & 0x3) << 2))
	return (rev3((pat & 0xfc00) >> 10) << 10)
		| (rev2((pat & 0x03c0) >> 6) << 6)
		| rev3((pat & 0x003f));
#undef rev3
#undef rev2
}

static int
pat_90rot(int pat)
{
	/* Rotate by 90 degrees:
	 * 5 6 7    7 4 2
	 * 3   4 -> 6   1
	 * 0 1 2    5 3 0 */
	/* I'm too lazy to optimize this :) */
	int vals[8];
	for (int i = 0; i < 8; i++)
		vals[i] = (pat >> (i * 2)) & 0x3;
	int vals2[8];
	vals2[0] = vals[5]; vals2[1] = vals[3]; vals2[2] = vals[0];
	vals2[3] = vals[6];                     vals2[4] = vals[1];
	vals2[5] = vals[7]; vals2[6] = vals[4]; vals2[7] = vals[2];
	int p2 = 0;
	for (int i = 0; i < 8; i++)
		p2 |= vals2[i] << (i * 2);
	return p2;
}

static void
pattern_gen(char *table, int pat, char *src, int srclen, int fixed_color)
{
	for (; srclen > 0; src++, srclen--) {
		if (srclen == 5)
			continue;
		int patofs = (srclen > 5 ? srclen - 1 : srclen) - 1;
		switch (*src) {
			case '?':
				*src = '.'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = '?'; // for future recursions
				return;
			case 'x':
				*src = '.'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = 'x'; // for future recursions
				return;
			case 'o':
				*src = '.'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(table, pat, src, srclen, fixed_color);
				*src = 'o'; // for future recursions
				return;
			case '.': /* 0 */ break;
			case 'X': pat |= S_BLACK << (patofs * 2); break;
			case 'O': pat |= S_WHITE << (patofs * 2); break;
			case '#': pat |= S_OFFBOARD << (patofs * 2); break;
		}
	}

	/* Original pattern, all transpositions and rotations */
	pattern_record(table, src - 9, pat, fixed_color);
	pattern_record(table, src - 9, pat_vmirror(pat), fixed_color);
	pattern_record(table, src - 9, pat_hmirror(pat), fixed_color);
	pattern_record(table, src - 9, pat_vmirror(pat_hmirror(pat)), fixed_color);
	pattern_record(table, src - 9, pat_90rot(pat), fixed_color);
	pattern_record(table, src - 9, pat_90rot(pat_vmirror(pat)), fixed_color);
	pattern_record(table, src - 9, pat_90rot(pat_hmirror(pat)), fixed_color);
	pattern_record(table, src - 9, pat_90rot(pat_vmirror(pat_hmirror(pat))), fixed_color);
}

#warning gcc is stupid; ignore following out-of-bounds warnings

static void
patterns_gen(struct playout_policy *p, char src[][11], int src_n)
{
	struct moggy_policy *pp = p->data;

	for (int i = 0; i < src_n; i++) {
		//printf("<%s>\n", src[i]);
		int fixed_color = 0;
		switch (src[i][9]) {
			case 'X': fixed_color = S_BLACK; break;
			case 'O': fixed_color = S_WHITE; break;
		}
		//fprintf(stderr, "** %s **\n", src[i]);
		pattern_gen(pp->patterns, 0, src[i], 9, fixed_color);
	}
}

static bool
patterns_load(char src[][11], int src_n, char *filename)
{
	FILE *f = fopen("moggy.patterns", "r");
	if (!f) return false;

	int i;
	for (i = 0; i < moggy_patterns_src_n; i++) {
		char line[32];
		if (!fgets(line, sizeof(line), f))
			goto error;
		int l = strlen(line);
		if (l != 10 + (line[l - 1] == '\n'))
			goto error;
		memcpy(src[i], line, 10);
	}
	fprintf(stderr, "moggy.patterns: %d patterns loaded\n", i);
	fclose(f);
	return true;
error:
	fprintf(stderr, "Error loading moggy.patterns.\n");
	fclose(f);
	return false;
}

static void
patterns_init(struct playout_policy *p)
{
	char src[moggy_patterns_src_n][11];

	if (!patterns_load(src, moggy_patterns_src_n, "moggy.patterns")) {
		/* Use default pattern set. */
		for (int i = 0; i < moggy_patterns_src_n; i++)
			strcpy(src[i], moggy_patterns_src[i]);
	}

	patterns_gen(p, src, moggy_patterns_src_n);
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
	return (hashtable[pat] & m->color) && !is_bad_selfatari(b, m->color, m->coord);
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
apply_pattern(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;
	struct move_queue q;
	q.moves = 0;

	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return pass;

	foreach_neighbor(b, m->coord, {
		struct move m2; m2.coord = c; m2.color = stone_other(m->color);
		if (board_is_valid_move(b, &m2))
			apply_pattern_here(p, pp->patterns, b, &m2, &q);
	});
	foreach_diag_neighbor(b, m->coord) {
		struct move m2; m2.coord = c; m2.color = stone_other(m->color);
		if (board_is_valid_move(b, &m2))
			apply_pattern_here(p, pp->patterns, b, &m2, &q);
	} foreach_diag_neighbor_end;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Pattern candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
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
			       || board_group_info(b, group_at(b, c3)).libs < 1; \
				return false; \
		} \
		enum stone s2 = board_atxy(b, x + (xd2_), y + (yd2_)); \
		if (s2 == lcolor) return false; \
		/* Then, can X actually "play" 1 in the ladder? */ \
		if (neighbor_count_at(b, c1, lcolor) + neighbor_count_at(b, c1, S_OFFBOARD) >= 2) \
			return false; /* It would be self-atari! */ \
	}
#define ladder_horiz	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d horiz step (%d,%d)\n", x, y, xd, yd); x += xd; ladder_check(xd, 0, -2 * xd, yd, 0, yd); } while (0)
#define ladder_vert	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d vert step of (%d,%d)\n", x, y, xd, yd); y += yd; ladder_check(0, yd, xd, -2 * yd, xd, 0); } while (0)

	if (ladder_catcher(b, x - xd, y, lcolor))
		ladder_horiz;
	do {
		ladder_vert;
		ladder_horiz;
	} while (1);
}


static void
group_atari_check(struct playout_policy *p, struct board *b, group_t group, enum stone to_play, struct move_queue *q)
{
	struct moggy_policy *pp = p->data;
	int qmoves_prev = q->moves;

	/* We don't use @to_play almost anywhere since any moves here are good
	 * for both defender and attacker. */

	enum stone color = board_at(b, group_base(group));
	coord_t lib = board_group_info(b, group).lib[0];

	assert(color != S_OFFBOARD && color != S_NONE);
	if (PLDEBUGL(5))
		fprintf(stderr, "atariiiiiiiii %s of color %d\n", coord2sstr(lib, b), color);
	assert(board_at(b, lib) == S_NONE);

	/* Do not bother with kos. */
	if (group_is_onestone(b, group)
	    && neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) == 4)
		return;

	/* Can we capture some neighbor? */
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != stone_other(color)
			    || board_group_info(b, group_at(b, c)).libs > 1)
				continue;

			coord_t capture = board_group_info(b, group_at(b, c)).lib[0];
			if (PLDEBUGL(6))
				fprintf(stderr, "can capture group %d (%s)?\n",
					group_at(b, c), coord2sstr(capture, b));
			struct move m; m.color = to_play; m.coord = capture;
			/* Does that move even make sense? */
			if (!board_is_valid_move(b, &m))
				continue;
			/* Make sure capturing the group will actually
			 * do us any good. */
			else if (is_bad_selfatari(b, to_play, capture))
				continue;

			q->move[q->moves++] = capture;
			mq_nodup(q);
		});
	} foreach_in_group_end;

	struct move m; m.color = to_play; m.coord = lib;
	if (!board_is_valid_move(b, &m))
		return;

	/* Do not suicide... */
	if (is_bad_selfatari(b, to_play, lib))
		return;
	if (PLDEBUGL(6))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders. */
	if (pp->ladders && ladder_catches(p, b, lib, group)) {
		return;
	}
	if (PLDEBUGL(6))
		fprintf(stderr, "...no ladder\n");

	if (to_play != color) {
		/* We are the attacker! In that case, throw away the moves
		 * that defend our groups, since we can capture the culprit. */
		q->moves = qmoves_prev;
	}

	q->move[q->moves++] = lib;
	mq_nodup(q);
}

static coord_t
global_atari_check(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct move_queue q;
	q.moves = 0;

	if (b->clen == 0)
		return pass;

	int g_base = fast_random(b->clen);
	for (int g = g_base; g < b->clen; g++) {
		group_atari_check(p, b, group_at(b, group_base(b->c[g])), to_play, &q);
		if (q.moves > 0)
			return q.move[fast_random(q.moves)];
	}
	for (int g = 0; g < g_base; g++) {
		group_atari_check(p, b, group_at(b, group_base(b->c[g])), to_play, &q);
		if (q.moves > 0)
			return q.move[fast_random(q.moves)];
	}
	return pass;
}

static coord_t
local_atari_check(struct playout_policy *p, struct board *b, struct move *m)
{
	struct move_queue q;
	q.moves = 0;

	/* Did the opponent play a self-atari? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 1) {
		group_atari_check(p, b, group_at(b, m->coord), stone_other(m->color), &q);
	}

	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;
		group_atari_check(p, b, g, stone_other(m->color), &q);
	});

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Local atari candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	int i = fast_random(q.moves);
	return q.moves ? q.move[i] : pass;
}

coord_t
playout_moggy_choose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct moggy_policy *pp = p->data;
	coord_t c;

	if (PLDEBUGL(5))
		board_print(b, stderr);

	/* Local checks */
	if (!is_pass(b->last_move.coord)) {
		/* Local group in atari? */
		if (pp->lcapturerate > fast_random(100)) {
			c = local_atari_check(p, b, &b->last_move);
			if (!is_pass(c))
				return c;
		}

		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			c = apply_pattern(p, b, &b->last_move);
			if (!is_pass(c))
				return c;
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > fast_random(100)) {
		c = global_atari_check(p, b, to_play);
		if (!is_pass(c))
			return c;
	}

	return pass;
}

static int
assess_local_bonus(struct playout_policy *p, struct board *board, struct move *a, struct move *b, int games)
{
	struct moggy_policy *pp = p->data;
	if (!pp->assess_local)
		return games;

	int dx = abs(coord_x(a->coord, board) - coord_x(b->coord, board));
	int dy = abs(coord_y(a->coord, board) - coord_y(b->coord, board));
	/* adjecent move, directly or diagonally? */
	if (dx + dy <= 1 + (dx && dy))
		return games;
	else
		return games / 2;
}

int
playout_moggy_assess(struct playout_policy *p, struct board *b, struct move *m, int games)
{
	struct moggy_policy *pp = p->data;

	if (is_pass(m->coord))
		return 0;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "ASSESS of %s:\n", coord2sstr(m->coord, b));
		board_print(b, stderr);
	}

	/* Are we dealing with atari? */
	if (pp->lcapturerate || pp->capturerate) {
		bool ladder = false;

		foreach_neighbor(b, m->coord, {
			group_t g = group_at(b, c);
			if (!g || board_group_info(b, g).libs != 1)
				continue;

			/* _Never_ play here if this move plays out
			 * a caught ladder. (Unless it captures another
			 * group. :-) */
			if (pp->ladderassess && ladder_catches(p, b, m->coord, g)) {
				/* Note that the opposite is not guarded against;
				 * we do not advise against capturing a laddered
				 * group (but we don't encourage it either). Such
				 * a move can simplify tactical situations if we
				 * can afford it. */
				if (m->color == board_at(b, c))
					ladder = true;
				continue;
			}

			struct move_queue q; q.moves = 0;
			group_atari_check(p, b, g, m->color, &q);
			while (q.moves--)
				if (q.move[q.moves] == m->coord) {
					if (PLDEBUGL(5))
						fprintf(stderr, "1.0: atari\n");
					return assess_local_bonus(p, b, &b->last_move, m, games) * 2;
				}
		});

		if (ladder) {
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: ladder\n");
			return -games;
		}
	}

	/* Is this move a self-atari? */
	if (pp->selfatarirate) {
		if (is_bad_selfatari(b, m->color, m->coord)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: self-atari\n");
			return -games;
		}
	}

	/* Pattern check */
	if (pp->patternrate) {
		if (test_pattern_here(p, pp->patterns, b, m)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: pattern\n");
			return assess_local_bonus(p, b, &b->last_move, m, games);
		}
	}

	return 0;
}

bool
playout_moggy_permit(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;

	/* The idea is simple for now - never allow self-atari moves.
	 * They suck in general, but this also permits us to actually
	 * handle seki in the playout stage. */
#if 0
	fprintf(stderr, "__ sar test? %s %s\n", stone2str(m->color), coord2sstr(m->coord, b));
	if (is_bad_selfatari(b, m->color, m->coord))
		fprintf(stderr, "__ Prohibiting self-atari %s %s\n", stone2str(m->color), coord2sstr(m->coord, b));
#endif
	return fast_random(100) >= pp->selfatarirate || !is_bad_selfatari(b, m->color, m->coord);
}


struct playout_policy *
playout_moggy_init(char *arg)
{
	struct playout_policy *p = calloc(1, sizeof(*p));
	struct moggy_policy *pp = calloc(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_moggy_choose;
	p->assess = playout_moggy_assess;
	p->permit = playout_moggy_permit;

	int rate = 90;

	pp->lcapturerate = pp->capturerate = pp->patternrate = pp->selfatarirate = -1;
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
			} else if (!strcasecmp(optname, "selfatarirate") && optval) {
				pp->selfatarirate = atoi(optval);
			} else if (!strcasecmp(optname, "rate") && optval) {
				rate = atoi(optval);
			} else if (!strcasecmp(optname, "ladders")) {
				pp->ladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "borderladders")) {
				pp->borderladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "ladderassess")) {
				pp->ladderassess = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "assess_local")) {
				pp->assess_local = optval && *optval == '0' ? false : true;
			} else {
				fprintf(stderr, "playout-moggy: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}
	if (pp->lcapturerate == -1) pp->lcapturerate = rate;
	if (pp->capturerate == -1) pp->capturerate = rate;
	if (pp->patternrate == -1) pp->patternrate = rate;
	if (pp->selfatarirate == -1) pp->selfatarirate = rate;

	patterns_init(p);

	return p;
}
