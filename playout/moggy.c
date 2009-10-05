#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "pattern3.h"
#include "playout.h"
#include "playout/moggy.h"
#include "random.h"
#include "tactics.h"
#include "uct/prior.h"

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)


/* Note that the context can be shared by multiple threads! */

struct moggy_policy {
	bool ladders, ladderassess, borderladders, assess_local;
	int lcapturerate, atarirate, capturerate, patternrate;
	int selfatarirate;
	int fillboardtries;
	/* Whether to look for patterns around second-to-last move. */
	bool pattern2;

	struct pattern3s patterns;
};


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
	"???", /* o?? has some pathological tsumego cases */
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
apply_pattern_here(struct playout_policy *p,
		struct board *b, struct move *m, struct move_queue *q)
{
	struct moggy_policy *pp = p->data;
	if (test_pattern3_here(&pp->patterns, b, m))
		mq_add(q, m->coord);
}

/* Check if we match any pattern around given move (with the other color to play). */
static coord_t
apply_pattern(struct playout_policy *p, struct board *b, struct move *m, struct move *mm)
{
	struct move_queue q;
	q.moves = 0;

	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return pass;

	foreach_neighbor(b, m->coord, {
		struct move m2; m2.coord = c; m2.color = stone_other(m->color);
		if (board_is_valid_move(b, &m2))
			apply_pattern_here(p, b, &m2, &q);
	});
	foreach_diag_neighbor(b, m->coord) {
		struct move m2; m2.coord = c; m2.color = stone_other(m->color);
		if (board_is_valid_move(b, &m2))
			apply_pattern_here(p, b, &m2, &q);
	} foreach_diag_neighbor_end;

	if (mm) { /* Second move for pattern searching */
		foreach_neighbor(b, mm->coord, {
			if (coord_is_8adjecent(m->coord, c, b))
				continue;
			struct move m2; m2.coord = c; m2.color = stone_other(m->color);
			if (board_is_valid_move(b, &m2))
				apply_pattern_here(p, b, &m2, &q);
		});
		foreach_diag_neighbor(b, mm->coord) {
			if (coord_is_8adjecent(m->coord, c, b))
				continue;
			struct move m2; m2.coord = c; m2.color = stone_other(m->color);
			if (board_is_valid_move(b, &m2))
				apply_pattern_here(p, b, &m2, &q);
		} foreach_diag_neighbor_end;
	}

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Pattern candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	return mq_pick(&q);
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

	enum stone lcolor = board_at(b, group_base(laddered));
	int x = coord_x(coord, b), y = coord_y(coord, b);

	if (PLDEBUGL(6))
		fprintf(stderr, "ladder check - does %s play out %s's laddered group %s?\n",
			coord2sstr(coord, b), stone2str(lcolor), coord2sstr(laddered, b));

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

	if (!pp->ladders)
		return false;

	/* Figure out the ladder direction */
	int xd, yd;
	xd = board_atxy(b, x + 1, y) == S_NONE ? 1 : board_atxy(b, x - 1, y) == S_NONE ? -1 : 0;
	yd = board_atxy(b, x, y + 1) == S_NONE ? 1 : board_atxy(b, x, y - 1) == S_NONE ? -1 : 0;

	if (!xd || !yd) {
		if (PLDEBUGL(5))
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
		if (PLDEBUGL(5))
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
		if (PLDEBUGL(5)) \
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
#define ladder_horiz	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d horiz step (%d,%d)\n", x, y, xd, yd); x += xd; ladder_check(xd, 0, -2 * xd, yd, 0, yd); } while (0)
#define ladder_vert	do { if (PLDEBUGL(6)) fprintf(stderr, "%d,%d vert step of (%d,%d)\n", x, y, xd, yd); y += yd; ladder_check(0, yd, xd, -2 * yd, xd, 0); } while (0)

	if (ladder_catcher(b, x - xd, y, lcolor))
		ladder_horiz;
	do {
		ladder_vert;
		ladder_horiz;
	} while (1);
}


static coord_t
can_be_captured(struct playout_policy *p, struct board *b, enum stone capturer, coord_t c, enum stone to_play)
{
	if (board_at(b, c) != stone_other(capturer)
	    || board_group_info(b, group_at(b, c)).libs > 1)
		return pass;

	coord_t capture = board_group_info(b, group_at(b, c)).lib[0];
	if (PLDEBUGL(6))
		fprintf(stderr, "can capture group %d (%s)?\n",
			group_at(b, c), coord2sstr(capture, b));
	struct move m; m.color = to_play; m.coord = capture;
	/* Does that move even make sense? */
	if (!board_is_valid_move(b, &m))
		return pass;
	/* Make sure capturing the group will actually
	 * do us any good. */
	else if (is_bad_selfatari(b, to_play, capture))
		return pass;

	return capture;
}

static bool
can_be_rescued(struct playout_policy *p, struct board *b, group_t group, enum stone color, coord_t lib)
{
	/* Does playing on the liberty rescue the group? */
	if (!is_bad_selfatari(b, color, lib))
		return true;

	/* Then, maybe we can capture one of our neighbors? */
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (!is_pass(can_be_captured(p, b, color, c, color)))
				return true;
		});
	} foreach_in_group_end;
	return false;
}

static void
group_atari_check(struct playout_policy *p, struct board *b, group_t group, enum stone to_play, struct move_queue *q, coord_t *ladder)
{
	int qmoves_prev = q->moves;

	/* We don't use @to_play almost anywhere since any moves here are good
	 * for both defender and attacker. */

	enum stone color = board_at(b, group_base(group));
	coord_t lib = board_group_info(b, group).lib[0];

	assert(color != S_OFFBOARD && color != S_NONE);
	if (PLDEBUGL(5))
		fprintf(stderr, "[%s] atariiiiiiiii %s of color %d\n", coord2sstr(group, b), coord2sstr(lib, b), color);
	assert(board_at(b, lib) == S_NONE);

	/* Do not bother with kos. */
	if (group_is_onestone(b, group)
	    && neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) == 4)
		return;

	/* Can we capture some neighbor? */
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			coord_t capture = can_be_captured(p, b, color, c, to_play);
			if (is_pass(capture))
				continue;

			mq_add(q, capture);
			mq_nodup(q);
		});
	} foreach_in_group_end;

	struct move m; m.color = to_play; m.coord = lib;
	if (!board_is_valid_move(b, &m))
		return;

	/* Do not suicide... */
	if (is_bad_selfatari(b, to_play, lib))
		return;
	/* Do not remove group that cannot be saved by the opponent. */
	if (to_play != color && !can_be_rescued(p, b, group, color, lib))
		return;
	if (PLDEBUGL(6))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders. */
	if (ladder_catches(p, b, lib, group)) {
		/* Sometimes we want to keep the ladder move in the
		 * queue in order to discourage it. */
		if (!ladder)
			return;
		else
			*ladder = lib;
	}
	if (PLDEBUGL(6))
		fprintf(stderr, "...no ladder\n");

	if (to_play != color) {
		/* We are the attacker! In that case, throw away the moves
		 * that defend our groups, since we can capture the culprit. */
		q->moves = qmoves_prev;
	}

	mq_add(q, lib);
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
		group_atari_check(p, b, group_at(b, group_base(b->c[g])), to_play, &q, NULL);
		if (q.moves > 0)
			return mq_pick(&q);
	}
	for (int g = 0; g < g_base; g++) {
		group_atari_check(p, b, group_at(b, group_base(b->c[g])), to_play, &q, NULL);
		if (q.moves > 0)
			return mq_pick(&q);
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
		group_atari_check(p, b, group_at(b, m->coord), stone_other(m->color), &q, NULL);
	}

	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;
		group_atari_check(p, b, g, stone_other(m->color), &q, NULL);
	});

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Local atari candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	return mq_pick(&q);
}

static bool
miai_2lib(struct board *b, group_t group, enum stone color)
{
	bool can_connect = false, can_pull_out = false;
	/* We have miai if we can either connect on both libs,
	 * or connect on one lib and escape on another. (Just
	 * having two escape routes can be risky.) */
	foreach_neighbor(b, board_group_info(b, group).lib[0], {
		enum stone cc = board_at(b, c);
		if (cc == S_NONE && cc != board_group_info(b, group).lib[1]) {
			can_pull_out = true;
		} else if (cc != color) {
			continue;
		}

		group_t cg = group_at(b, c);
		if (cg && cg != group && board_group_info(b, cg).libs > 1)
			can_connect = true;
	});
	foreach_neighbor(b, board_group_info(b, group).lib[1], {
		enum stone cc = board_at(b, c);
		if (cc == S_NONE && cc != board_group_info(b, group).lib[0] && can_connect) {
			return true;
		} else if (cc != color) {
			continue;
		}

		group_t cg = group_at(b, c);
		if (cg && cg != group && board_group_info(b, cg).libs > 1)
			return (can_connect || can_pull_out);
	});
	return false;
}

static void
group_2lib_check(struct playout_policy *p, struct board *b, group_t group, enum stone to_play, struct move_queue *q)
{
	enum stone color = board_at(b, group_base(group));
	assert(color != S_OFFBOARD && color != S_NONE);

	if (PLDEBUGL(5))
		fprintf(stderr, "[%s] 2lib check of color %d\n",
			coord2sstr(group, b), color);

	/* Do not try to atari groups that cannot be harmed. */
	if (miai_2lib(b, group, color))
		return;

	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, group).lib[i];
		assert(board_at(b, lib) == S_NONE);
		struct move m; m.color = to_play; m.coord = lib;
		if (!board_is_valid_move(b, &m))
			continue;

		/* Don't play at the spot if it is extremely short
		 * of liberties... */
		/* XXX: This looks harmful, could significantly
		 * prefer atari to throwin:
		 *
		 * XXXOOOOOXX
		 * .OO.....OX
		 * XXXOOOOOOX */
#if 0
		if (neighbor_count_at(b, lib, stone_other(color)) + immediate_liberty_count(b, lib) < 2)
			continue;
#endif

		/* If the owner can't play at the spot, we don't want
		 * to bother either. */
		if (is_bad_selfatari(b, color, lib))
			continue;

		/* Of course we don't want to play bad selfatari
		 * ourselves, if we are the attacker... */
		if (to_play != color && is_bad_selfatari(b, to_play, lib))
			continue;

		/* Tasty! Crispy! Good! */
		mq_add(q, lib);
	}
}

static coord_t
local_2lib_check(struct playout_policy *p, struct board *b, struct move *m)
{
	struct move_queue q;
	q.moves = 0;

	/* Does the opponent have just two liberties? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 2) {
		group_2lib_check(p, b, group_at(b, m->coord), stone_other(m->color), &q);
#if 0
		/* We always prefer to take off an enemy chain liberty
		 * before pulling out ourselves. */
		/* XXX: We aren't guaranteed to return to that group
		 * later. */
		if (q.moves)
			return q.move[fast_random(q.moves)];
#endif
	}

	/* Then he took a third liberty from neighboring chain? */
	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 2)
			continue;
		group_2lib_check(p, b, g, stone_other(m->color), &q);
	});

	if (PLDEBUGL(5)) {
		fprintf(stderr, "Local 2lib candidate moves: ");
		for (int i = 0; i < q.moves; i++) {
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
		}
		fprintf(stderr, "\n");
	}

	return mq_pick(&q);
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

		/* Local group can be PUT in atari? */
		if (pp->atarirate > fast_random(100)) {
			c = local_2lib_check(p, b, &b->last_move);
			if (!is_pass(c))
				return c;
		}

		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			c = apply_pattern(p, b, &b->last_move,
			                  pp->pattern2 && b->last_move2.coord >= 0 ? &b->last_move2 : NULL);
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

	/* Fill board */
	int fbtries = b->flen / 8;
	for (int i = 0; i < (fbtries < pp->fillboardtries ? fbtries : pp->fillboardtries); i++) {
		coord_t coord = b->f[fast_random(b->flen)];
		if (is_pass(coord) || immediate_liberty_count(b, coord) != 4)
			continue;
		foreach_diag_neighbor(b, coord) {
			if (board_at(b, c) != S_NONE)
				goto next_try;
		} foreach_diag_neighbor_end;
		return coord;
next_try:;
	}

	return pass;
}


static int
assess_local_bonus(struct playout_policy *p, struct board *board, coord_t a, coord_t b, int games)
{
	struct moggy_policy *pp = p->data;
	if (!pp->assess_local)
		return games;

	int dx = abs(coord_x(a, board) - coord_x(b, board));
	int dy = abs(coord_y(a, board) - coord_y(b, board));
	/* adjecent move, directly or diagonally? */
	if (dx + dy <= 1 + (dx && dy))
		return games;
	else
		return games / 2;
}

void
playout_moggy_assess_group(struct playout_policy *p, struct prior_map *map, group_t g, int games)
{
	struct moggy_policy *pp = p->data;
	struct board *b = map->b;
	struct move_queue q; q.moves = 0;

	if (board_group_info(b, g).libs > 2)
		return;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "ASSESS of group %s:\n", coord2sstr(g, b));
		board_print(b, stderr);
	}

	if (board_group_info(b, g).libs == 2) {
		if (!pp->atarirate)
			return;
		group_2lib_check(p, b, g, map->to_play, &q);
		while (q.moves--) {
			coord_t coord = q.move[q.moves];
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: 2lib %s\n", coord2sstr(coord, b));
			int assess = assess_local_bonus(p, b, b->last_move.coord, coord, games) / 2;
			add_prior_value(map, coord, assess, assess);
		}
		return;
	}

	/* This group, sir, is in atari! */

	if (!pp->capturerate && !pp->lcapturerate && !pp->ladderassess)
		return;

	coord_t ladder = pass;
	group_atari_check(p, b, g, map->to_play, &q, &ladder);
	while (q.moves--) {
		coord_t coord = q.move[q.moves];

		/* _Never_ play here if this move plays out
		 * a caught ladder. */
		if (coord == ladder) {
			/* Note that the opposite is not guarded against;
			 * we do not advise against capturing a laddered
			 * group (but we don't encourage it either). Such
			 * a move can simplify tactical situations if we
			 * can afford it. */
			if (!pp->ladderassess || map->to_play != board_at(b, g))
				continue;
			/* FIXME: We give the malus even if this move
			 * captures another group. */
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: ladder %s\n", coord2sstr(coord, b));
			add_prior_value(map, coord, -games, games);
			continue;
		}

		if (!pp->capturerate && !pp->lcapturerate)
			continue;

		if (PLDEBUGL(5))
			fprintf(stderr, "1.0: atari %s\n", coord2sstr(coord, b));
		int assess = assess_local_bonus(p, b, b->last_move.coord, coord, games) * 2;
		add_prior_value(map, coord, assess, assess);
	}
}

int
playout_moggy_assess_one(struct playout_policy *p, struct prior_map *map, coord_t coord, int games)
{
	struct moggy_policy *pp = p->data;
	struct board *b = map->b;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "ASSESS of move %s:\n", coord2sstr(coord, b));
		board_print(b, stderr);
	}

	/* Is this move a self-atari? */
	if (pp->selfatarirate) {
		if (is_bad_selfatari(b, map->to_play, coord)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: self-atari\n");
			return -games;
		}
	}

	/* Pattern check */
	if (pp->patternrate) {
		struct move m = { .color = map->to_play, .coord = coord };
		if (test_pattern3_here(&pp->patterns, b, &m)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: pattern\n");
			return assess_local_bonus(p, b, b->last_move.coord, coord, games);
		}
	}

	return 0;
}

void
playout_moggy_assess(struct playout_policy *p, struct prior_map *map, int games)
{
	struct moggy_policy *pp = p->data;

	/* First, go through all endangered groups. */
	if (pp->lcapturerate || pp->capturerate || pp->atarirate || pp->ladderassess)
		for (group_t g = 1; g < board_size2(map->b); g++)
			if (group_at(map->b, g) == g)
				playout_moggy_assess_group(p, map, g, games);

	/* Then, assess individual moves. */
	if (!pp->patternrate && !pp->selfatarirate)
		return;
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		int assess = playout_moggy_assess_one(p, map, c, games);
		if (!assess)
			continue;
		add_prior_value(map, c, assess, abs(assess));
	} foreach_point_end;
}

bool
playout_moggy_permit(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;

	/* The idea is simple for now - never allow self-atari moves.
	 * They suck in general, but this also permits us to actually
	 * handle seki in the playout stage. */

	if (fast_random(100) >= pp->selfatarirate) {
		if (PLDEBUGL(5))
			fprintf(stderr, "skipping sar test\n");
		return true;
	}
	bool selfatari = is_bad_selfatari(b, m->color, m->coord);
	if (PLDEBUGL(5) && selfatari)
		fprintf(stderr, "__ Prohibiting self-atari %s %s\n",
			stone2str(m->color), coord2sstr(m->coord, b));
	return !selfatari;
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

	pp->lcapturerate = pp->atarirate = pp->capturerate = pp->patternrate = pp->selfatarirate = -1;
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
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				pp->atarirate = atoi(optval);
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "selfatarirate") && optval) {
				pp->selfatarirate = atoi(optval);
			} else if (!strcasecmp(optname, "rate") && optval) {
				rate = atoi(optval);
			} else if (!strcasecmp(optname, "fillboardtries")) {
				pp->fillboardtries = atoi(optval);
			} else if (!strcasecmp(optname, "ladders")) {
				pp->ladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "borderladders")) {
				pp->borderladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "ladderassess")) {
				pp->ladderassess = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "assess_local")) {
				pp->assess_local = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "pattern2")) {
				pp->pattern2 = optval && *optval == '0' ? false : true;
			} else {
				fprintf(stderr, "playout-moggy: Invalid policy argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}
	if (pp->lcapturerate == -1) pp->lcapturerate = rate;
	if (pp->atarirate == -1) pp->atarirate = rate;
	if (pp->capturerate == -1) pp->capturerate = rate;
	if (pp->patternrate == -1) pp->patternrate = rate;
	if (pp->selfatarirate == -1) pp->selfatarirate = rate;

	pattern3s_init(&pp->patterns, moggy_patterns_src, moggy_patterns_src_n);

	return p;
}
