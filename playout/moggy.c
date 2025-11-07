/* Heuristical playout (and tree prior) policy modelled primarily after
 * the description of the Mogo engine. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "joseki/joseki.h"
#include "mq.h"
#include "gmq.h"
#include "mtmq.h"
#include "pattern3.h"
#include "playout.h"
#include "playout/moggy.h"
#include "random.h"
#include "tactics/1lib.h"
#include "tactics/2lib.h"
#include "tactics/nlib.h"
#include "tactics/ladder.h"
#include "tactics/nakade.h"
#include "tactics/selfatari.h"
#include "tactics/seki.h"
#include "uct/prior.h"

/* In case "seqchoose" move picker is enabled (i.e. no "fullchoose"
 * parameter passed), we stochastically apply fixed set of decision
 * rules in given order.
 *
 * In "fullchoose" mode, we instead build a move queue of variously
 * tagged candidates, then consider a probability distribution over
 * them and pick a move from that. */

/* Move queue tags. Some may be even undesirable - these moves then
 * receive a penalty; penalty tags should be used only when it is
 * certain the move would be considered anyway. */
enum mq_tag {
	MQ_KO = 0,
	MQ_LATARI,
	MQ_L2LIB,
#define MQ_LADDER MQ_L2LIB /* XXX: We want to fit in char still! */
	MQ_LNLIB,
	MQ_PAT3,
	MQ_GATARI,
	MQ_JOSEKI,
	MQ_NAKADE,
	MQ_MAX
};


#define PAT3_N 15

/* Note that the context can be shared by multiple threads! */

typedef struct {
	unsigned int lcapturerate, atarirate, nlibrate, ladderrate, capturerate, patternrate, korate, josekirate, nakaderate, eyefixrate;
	unsigned int selfatarirate, eyefillrate, alwaysccaprate;
	unsigned int fillboardtries;
	int koage;
	/* Whether to look for patterns around second-to-last move. */
	bool pattern2;
	/* Whether, when self-atari attempt is detected, to play the other
	 * group's liberty if that is non-self-atari. */
	bool selfatari_other;
	/* Whether to read out ladders elsewhere than near the board
	 * in the playouts. Note that such ladder testing is currently
	 * a fairly expensive operation. */
	bool middle_ladder;

	/* 1lib settings: */
	/* Whether to always pick from moves capturing all groups in
	 * global_atari_check(). */
	bool capcheckall;
	/* Prior stone weighting. Weight of each stone between
	 * cap_stone_min and cap_stone_max is (assess*100)/cap_stone_denom. */
	int cap_stone_min, cap_stone_max;
	int cap_stone_denom;

	/* 2lib settings: */
	bool atari_def_no_hopeless;
	bool atari_miaisafe;

	/* nlib settings: */
	int nlib_count;

	pattern3s_t patterns;

	fixp_t pat3_gammas[PAT3_N];

	/* Gamma values for queue tags - correspond to probabilities. */
	/* XXX: Tune. */
	bool fullchoose;
	double mq_prob[MQ_MAX], tenuki_prob;
} moggy_policy_t;

/* Per simulation state (moggy_policy is shared by all threads) */
typedef struct {
	/* Selfatari move rejected by permit() during the last move(s).
	 * Logic may not kick in immediately so we have room for both colors. */
	coord_t last_selfatari[S_MAX];
} moggy_state_t;

static char moggy_patterns_src[PAT3_N][11] = {
	/* hane pattern - enclosing hane */	/* 0.52 */
	"XOX"
	"..."
	"???",
	/* hane pattern - non-cutting hane */	/* 0.53 */
	"YO."
	"..."
	"?.?",
	/* hane pattern - magari */		/* 0.32 */
	"XO?"
	"X.."
	"x.?",
	/* hane pattern - thin hane */		/* 0.22 */
	"XOO"
	"..."
	"?.?" "X",
	/* generic pattern - katatsuke or diagonal attachment; similar to magari */	/* 0.37 */
	".Q."
	"Y.."
	"...",
	/* cut1 pattern (kiri) - unprotected cut */	/* 0.28 */
	"XO?"
	"O.o"
	"?o?",
	/* cut1 pattern (kiri) - peeped cut */	/* 0.21 */
	"XO?"
	"O.X"
	"???",
	/* cut2 pattern (de) */			/* 0.19 */
	"?X?"
	"O.O"
	"ooo",
	/* cut keima (not in Mogo) */		/* 0.82 */
	"OX?"
	"?.O"
	"?o?", /* oo? has some pathological tsumego cases */
	/* side pattern - chase */		/* 0.12 */
	"X.?"
	"O.?"
	"##?",
	/* side pattern - block side cut */	/* 0.20 */
	"OX?"
	"X.O"
	"###",
	/* side pattern - block side connection */	/* 0.11 */
	"?X?"
	"x.O"
	"###",
	/* side pattern - sagari (SUSPICIOUS) */	/* 0.16 */
	"?XQ"
	"x.x" /* Mogo has "x.?" */
	"###" /* Mogo has "X" */,
#if 0
	/* side pattern - throw-in (SUSPICIOUS) */
	"?OX"
	"o.O"
	"?##" "X",
#endif
	/* side pattern - cut (SUSPICIOUS) */	/* 0.57 */
	"?OY"
	"Y.O"
	"###" /* Mogo has "X" */,
	/* side pattern - eye piercing:
	 * # O O O .
	 * # O . O .
	 * # . . . .
	 * # # # # # */
	/* side pattern - make eye */		/* 0.44 */
	"?X."
	"Q.X"
	"###",
#if 0
	"Oxx"
	"..."
	"###",
#endif
};
#define moggy_patterns_src_n sizeof(moggy_patterns_src) / sizeof(moggy_patterns_src[0])

static inline bool
test_pattern3_here(playout_policy_t *p, board_t *b, move_t *m, bool middle_ladder, fixp_t *gamma)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	/* Check if 3x3 pattern is matched by given move... */
	char pi = -1;
	if (!pattern3_move_here(&pp->patterns, b, m, &pi))
		return false;
	/* ...and the move is not obviously stupid. */
	if (is_bad_selfatari(b, m->color, m->coord))
		return false;
	/* Ladder moves are stupid. */
	group_t atari_neighbor = board_get_atari_neighbor(b, m->coord, m->color);
	if (atari_neighbor && is_ladder(b, atari_neighbor, middle_ladder)
	    && !can_countercapture(b, atari_neighbor, NULL))
		return false;
	//fprintf(stderr, "%s: %d (%.3f)\n", coord2sstr(m->coord), (int) pi, pp->pat3_gammas[(int) pi]);
	*gamma = pp->pat3_gammas[(int) pi];
	return true;
}

static void
apply_pattern_here(playout_policy_t *p, board_t *b, coord_t c, enum stone color, gmq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	move_t m2 = move(c, color);
	fixp_t gamma;
	if (board_is_valid_move(b, &m2) && test_pattern3_here(p, b, &m2, pp->middle_ladder, &gamma)) {
		gmq_add(q, c, gamma);
	}
}

/* Check if we match any pattern around given move (with the other color to play). */
static void
apply_pattern(playout_policy_t *p, board_t *b, move_t *m, move_t *mm, gmq_t *q)
{
	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return;

	foreach_8neighbor(b, m->coord) {
		apply_pattern_here(p, b, c, stone_other(m->color), q);
	} foreach_8neighbor_end;

	if (mm) { /* Second move for pattern searching */
		foreach_8neighbor(b, mm->coord) {
			if (coord_is_8adjecent(m->coord, c))
				continue;
			apply_pattern_here(p, b, c, stone_other(m->color), q);
		} foreach_8neighbor_end;
	}

	if (DEBUGL(5) && q->moves)
		gmq_print(q, "Moggy pattern: ");
}

#ifdef MOGGY_JOSEKI
static void
joseki_check(playout_policy_t *p, board_t *b, enum stone to_play, mq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	if (!joseki_dict)
		return;

	foreach_joseki_move(joseki_dict, b, to_play) {
		if (!board_is_valid_play(b, to_play, c))
			continue;
		mq_add(q, c);
	} foreach_joseki_move_end;

	if (DEBUGL(5) && q->moves)
		mq_print_line(q, "Moggy joseki: ");
}
#endif /* MOGGY_JOSEKI */

static void
global_atari_check(playout_policy_t *p, board_t *b, enum stone to_play, mq_t *q)
{
	if (b->clen == 0)
		return;

	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	if (pp->capcheckall) {
		for (int g = 0; g < b->clen; g++)
			group_atari_check(pp->alwaysccaprate, b, group_at(b, group_base(b->c[g])), to_play, q, pp->middle_ladder);
		if (DEBUGL(5) && q->moves)
			mq_print_line(q, "Moggy global atari: ");
		if (pp->fullchoose)
			return;
	}

	int g_base = fast_random(b->clen);
	for (int g = g_base; g < b->clen; g++) {
		group_atari_check(pp->alwaysccaprate, b, group_at(b, group_base(b->c[g])), to_play, q, pp->middle_ladder);
		if (q->moves > 0) {
			/* XXX: Try carrying on. */
			if (DEBUGL(5))
				mq_print_line(q, "Moggy global atari: ");
			if (pp->fullchoose)
				return;
		}
	}
	for (int g = 0; g < g_base; g++) {
		group_atari_check(pp->alwaysccaprate, b, group_at(b, group_base(b->c[g])), to_play, q, pp->middle_ladder);
		if (q->moves > 0) {
			/* XXX: Try carrying on. */
			if (DEBUGL(5))
				mq_print_line(q, "Moggy global atari: ");
			if (pp->fullchoose)
				return;
		}
	}
}

static int
local_atari_check(playout_policy_t *p, board_t *b, move_t *m, mq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	int force = false;

	/* Did the opponent play a self-atari? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 1) {
		group_atari_check(pp->alwaysccaprate, b, group_at(b, m->coord), stone_other(m->color), q, pp->middle_ladder);
	}

	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;

		// Always defend big groups
		enum stone to_play = stone_other(m->color);
		enum stone color = board_at(b, group_base(g));
		if (to_play == color &&			// Defender
		    group_stone_count(b, g, 5) >= 3)
			force = true;
		
		group_atari_check(pp->alwaysccaprate, b, g, to_play, q, pp->middle_ladder);
	});

	if (q->moves &&
	    (force || pp->lcapturerate > fast_random(100))) {
		if (DEBUGL(5))
			mq_print_line(q, "Moggy local atari: ");
		return q->moves;
	}
	
	return 0;
}


static void
local_ladder_check(playout_policy_t *p, board_t *b, move_t *m, mq_t *q)
{
	group_t group = group_at(b, m->coord);

	if (board_group_info(b, group).libs != 2)
		return;

	for (int i = 0; i < 2; i++) {
		coord_t chase = board_group_info(b, group).lib[i];
		if (wouldbe_ladder(b, group, chase))
			mq_add(q, chase);
	}

	if (DEBUGL(5) && q->moves)
		mq_print_line(q, "Moggy ladder: ");
}


static void
local_2lib_check(playout_policy_t *p, board_t *b, move_t *m, mq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	group_t group = group_at(b, m->coord), group2 = 0;

	/* Does the opponent have just two liberties? */
	if (board_group_info(b, group).libs == 2) {
		group_2lib_check(b, group, stone_other(m->color), q, pp->atari_miaisafe, pp->atari_def_no_hopeless);
#if 0
		/* We always prefer to take off an enemy chain liberty
		 * before pulling out ourselves. */
		/* XXX: We aren't guaranteed to return to that group
		 * later. */
		if (q->moves)
			return q->move[fast_random(q->moves)];
#endif
	}

	/* Then he took a third liberty from neighboring chain? */
	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || g == group || g == group2 || board_group_info(b, g).libs != 2)
			continue;
		group_2lib_check(b, g, stone_other(m->color), q, pp->atari_miaisafe, pp->atari_def_no_hopeless);
		group2 = g; // prevent trivial repeated checks
	});

	if (DEBUGL(5) && q->moves)
		mq_print_line(q, "Moggy local 2lib: ");
}

static void
local_2lib_capture_check(playout_policy_t *p, board_t *b, move_t *m, mq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	group_t group = group_at(b, m->coord), group2 = 0;

	/* Nothing there normally since opponent avoided bad selfatari ... */
	if (board_group_info(b, group).libs == 2) {
		group_2lib_capture_check(b, group, stone_other(m->color), q, pp->atari_miaisafe, pp->atari_def_no_hopeless);
#if 0
		/* We always prefer to take off an enemy chain liberty
		 * before pulling out ourselves. */
		/* XXX: We aren't guaranteed to return to that group
		 * later. */
		if (q->moves)
			return q->move[fast_random(q->moves)];
#endif
	}

	/* Then he took a third liberty from neighboring chain? */
	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != m->color)  /* Not opponent group, skip */
			continue;
		group_t g = group_at(b, c);
		if (!g || g == group || g == group2 || board_group_info(b, g).libs != 2)
			continue;
		group_2lib_capture_check(b, g, stone_other(m->color), q, pp->atari_miaisafe, pp->atari_def_no_hopeless);
		group2 = g; // prevent trivial repeated checks
	});

	if (DEBUGL(5) && q->moves)
		mq_print_line(q, "Moggy local 2lib capture: ");
}

static void
local_nlib_check(playout_policy_t *p, board_t *b, move_t *m, mq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	enum stone color = stone_other(m->color);

	/* Attacking N-liberty groups in general is probably
	 * not feasible. What we are primarily concerned about is
	 * counter-attacking groups that have two physical liberties,
	 * but three effective liberties:
	 *
	 * . O . . . . #
	 * O O X X X X #
	 * . X O O X . #
	 * . X O . O X #
	 * . X O O . X #
	 * # # # # # # #
	 *
	 * The time for this to come is when the opponent took a liberty
	 * of ours, making a few-liberty group. Therefore, we focus
	 * purely on defense.
	 *
	 * There is a tradeoff - down to how many liberties we need to
	 * be to start looking? nlib_count=3 will work for the left black
	 * group (2lib-solver will suggest connecting the false eye), but
	 * not for top black group (it is too late to start playing 3-3
	 * capturing race). Also, we cannot prevent stupidly taking an
	 * outside liberty ourselves; the higher nlib_count, the higher
	 * the chance we withstand this.
	 *
	 * However, higher nlib_count means that we will waste more time
	 * checking non-urgent or alive groups, and we will play silly
	 * or wasted moves around alive groups. */

	group_t group2 = 0;
	foreach_8neighbor(b, m->coord) {
		group_t g = group_at(b, c);
		if (!g || group2 == g || board_at(b, c) != color)
			continue;
		if (board_group_info(b, g).libs < 3 || board_group_info(b, g).libs > pp->nlib_count)
			continue;
		group_nlib_defense_check(b, g, color, q);
		group2 = g; // prevent trivial repeated checks
	} foreach_8neighbor_end;

	if (DEBUGL(5) && q->moves)
		mq_print_line(q, "Moggy local nlib: ");
}

static coord_t
nakade_check(playout_policy_t *p, board_t *b, move_t *m, enum stone to_play)
{
	coord_t empty = pass;
	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != S_NONE)
			continue;
		if (is_pass(empty)) {
			empty = c;
			continue;
		}
		if (!coord_is_8adjecent(c, empty)) {
			/* Seems like impossible nakade
			 * shape! */
			return pass;
		}
	});
	assert(!is_pass(empty));

	coord_t nakade = nakade_point(b, empty, stone_other(to_play));
	if (DEBUGL(5) && !is_pass(nakade))
		fprintf(stderr, "Moggy nakade: %s\n", coord2sstr(nakade));
	return nakade;
}

static void
eye_fix_check(playout_policy_t *p, board_t *b, move_t *m, enum stone to_play, mq_t *q)
{
	/* The opponent could have filled an approach liberty for
	 * falsifying an eye like these:
	 *
	 * # # # # # #    X . X X O O  last_move == 1
	 * X X 2 O 1 O    X X 2 O 1 O  => suggest 2
	 * X . X X O .    X . X X O .
	 * X X O O . .    X X O O . O
	 *
	 * This case seems pretty common (e.g. Zen-Ishida game). */

	/* Iterator for walking coordinates in a clockwise fashion
	 * (nei8 jumps "over" the middle point, inst. of "around). */
	int stride = board_stride(b);
	int nei8_clockwise[10] = { -stride-1, 1, 1, stride, stride, -1, -1, -stride, -stride, 1 };

	/* This is sort of like a cross between foreach_diag_neighbor
	 * and foreach_8neighbor. */
	coord_t c = m->coord;
	for (int dni = 0; dni < 8; dni += 2) {
		// one diagonal neighbor
		coord_t c0 = c + nei8_clockwise[dni];
		// adjecent staight neighbor
		coord_t c1 = c0 + nei8_clockwise[dni + 1];
		// and adjecent another diagonal neighbor
		coord_t c2 = c1 + nei8_clockwise[dni + 2];

		/* The last move must have a pair of unfriendly diagonal
		 * neighbors separated by a friendly stone. */
		//fprintf(stderr, "inv. %s(%s)-%s(%s)-%s(%s), imm. libcount %d\n", coord2sstr(c0), stone2str(board_at(b, c0)), coord2sstr(c1), stone2str(board_at(b, c1)), coord2sstr(c2), stone2str(board_at(b, c2)), immediate_liberty_count(b, c1));
		if ((board_at(b, c0) == to_play || board_at(b, c0) == S_OFFBOARD)
		    && board_at(b, c1) == m->color
		    && (board_at(b, c2) == to_play || board_at(b, c2) == S_OFFBOARD)
		    /* The friendly stone then must have an empty neighbor... */
		    /* XXX: This works only for single stone, not e.g. for two
		     * stones in a row */
		    && immediate_liberty_count(b, c1) > 0) {
			foreach_neighbor(b, c1, {
				if (c == m->coord || board_at(b, c) != S_NONE)
					continue;
				/* ...and the neighbor must potentially falsify
				 * an eye. */
				coord_t falsifying = c;
				foreach_diag_neighbor(b, falsifying) {
					if (board_at(b, c) != S_NONE)
						continue;
					if (!board_is_eyelike(b, c, to_play))
						continue;
					/* We don't care about eyes that already
					 * _are_ false (board_is_false_eyelike())
					 * but that can become false. Therefore,
					 * either ==1 diagonal neighbor is
					 * opponent's (except in atari) or ==2
					 * are board edge. */
					coord_t falsified = c;
					int color_diag_libs[S_MAX] = {0};
					foreach_diag_neighbor(b, falsified) {
						if (board_at(b, c) == m->color && board_group_info(b, group_at(b, c)).libs == 1) {
							/* Suggest capturing a falsifying stone in atari. */
							mq_add(q, board_group_info(b, group_at(b, c)).lib[0]);
						} else {
							color_diag_libs[board_at(b, c)]++;
						}
					} foreach_diag_neighbor_end;
					if (color_diag_libs[m->color] == 1 || (color_diag_libs[m->color] == 0 && color_diag_libs[S_OFFBOARD] == 2)) {
						/* That's it. Fill the falsifying
						 * liberty before it's too late! */
						mq_add(q, falsifying);
					}
				} foreach_diag_neighbor_end;
			});
		}

		c = c1;
	}

	if (DEBUGL(5) && q->moves)
		mq_print_line(q, "Moggy eye fix: ");
}

static coord_t
fillboard_check(playout_policy_t *p, board_t *b)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	unsigned int fbtries = b->flen / 8;
	if (pp->fillboardtries < fbtries)
		fbtries = pp->fillboardtries;

	for (unsigned int i = 0; i < fbtries; i++) {
		coord_t coord = b->f[fast_random(b->flen)];
		if (immediate_liberty_count(b, coord) != 4)
			continue;
		foreach_diag_neighbor(b, coord) {
			if (board_at(b, c) != S_NONE)
				goto next_try;
		} foreach_diag_neighbor_end;
		return coord;
next_try:
		;
	}
	return pass;
}

static coord_t
playout_moggy_seqchoose(playout_policy_t *p, playout_setup_t *s, board_t *b, enum stone to_play)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	moggy_state_t *ps = (moggy_state_t*)b->ps;
	enum stone other_color = stone_other(to_play);

	/* Ko fight check */
	if (!is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage
	    && pp->korate > fast_random(100)) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			return b->last_ko.coord;
	}

	/* Local checks */
	if (!is_pass(last_move(b).coord)) {
		/* Local group in atari? */
		{  // pp->lcapturerate check in local_atari_check()
			mq_t q;  mq_init(&q);
			if (local_atari_check(p, b, &last_move(b), &q))
				return mq_pick(&q);
		}

		/* Local group trying to escape ladder? */
		if (pp->ladderrate > fast_random(100)) {
			mq_t q;  mq_init(&q);
			local_ladder_check(p, b, &last_move(b), &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Did we just reject selfatari move as opponent ?
		 * Check if his group can be laddered / put in atari */
		if (ps->last_selfatari[other_color] &&
		    pp->atarirate > fast_random(100)) {
			mq_t q;  mq_init(&q);
			move_t m = move(ps->last_selfatari[other_color], other_color);			
			ps->last_selfatari[other_color] = 0;  /* Clear */
			local_2lib_capture_check(p, b, &m, &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Local group can be PUT in atari? */
		if (pp->atarirate > fast_random(100)) {
			mq_t q;  mq_init(&q);
			local_2lib_check(p, b, &last_move(b), &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Local group reduced some of our groups to 3 libs? */
		if (pp->nlibrate > fast_random(100)) {
			mq_t q;  mq_init(&q);
			local_nlib_check(p, b, &last_move(b), &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Some other semeai-ish shape checks */
		if (pp->eyefixrate > fast_random(100)) {
			mq_t q;  mq_init(&q);
			eye_fix_check(p, b, &last_move(b), to_play, &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Nakade check */
		if (pp->nakaderate > fast_random(100)
		    && immediate_liberty_count(b, last_move(b).coord) > 0) {
			coord_t nakade = nakade_check(p, b, &last_move(b), to_play);
			if (!is_pass(nakade))
				return nakade;
		}

		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			gmq_t q;  gmq_init(&q);
			move_t *last_move2 = (pp->pattern2 && last_move2(b).coord >= 0 ? &last_move2(b) : NULL);
			apply_pattern(p, b, &last_move(b), last_move2, &q);
			if (q.moves > 0)
				return gmq_pick(&q);
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > fast_random(100)) {
		mq_t q;  mq_init(&q);
		global_atari_check(p, b, to_play, &q);
		if (q.moves > 0)
			return mq_pick(&q);
	}

#ifdef MOGGY_JOSEKI
	/* Joseki moves? */
	if (pp->josekirate > fast_random(100)) {
		mq_t q;  mq_init(&q);
		joseki_check(p, b, to_play, &q);
		if (q.moves > 0)
			return mq_pick(&q);
	}
#endif

	/* Fill board */
	if (pp->fillboardtries > 0) {
		coord_t c = fillboard_check(p, b);
		if (!is_pass(c))
			return c;
	}

	return pass;
}

/* Pick a move from queue q, giving different likelihoods to moves
 * based on their tags. */
static coord_t
mq_tagged_choose(playout_policy_t *p, board_t *b, enum stone to_play, mtmq_t *q)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;

	/* Now, cona_t probdist. */
	fixp_t total = 0;
	fixp_t pd[q->moves];
	for (int i = 0; i < q->moves; i++) {
		double val = 1.0;
		assert(q->tag[i] != 0);
		for (int j = 0; j < MQ_MAX; j++)
			if (q->tag[i] & (1<<j)) {
				//fprintf(stderr, "%s(%x) %d %f *= %f\n", coord2sstr(q->move[i]), q->tag[i], j, val, pp->mq_prob[j]);
				val *= pp->mq_prob[j];
			}
		pd[i] = double_to_fixp(val);
		total += pd[i];
	}
	total += double_to_fixp(pp->tenuki_prob);

	/* Finally, pick a move! */
	fixp_t stab = fast_irandom(total);
	if (DEBUGL(5)) {
		fprintf(stderr, "Pick (total %.3f stab %.3f): ", fixp_to_double(total), fixp_to_double(stab));
		for (int i = 0; i < q->moves; i++)
			fprintf(stderr, "%s(%x:%.3f) ", coord2sstr(q->move[i]), q->tag[i], fixp_to_double(pd[i]));
		fprintf(stderr, "\n");
	}
	for (int i = 0; i < q->moves; i++) {
		//fprintf(stderr, "%s(%x) %f (%f/%f)\n", coord2sstr(q->move[i]), q->tag[i], fixp_to_double(stab), fixp_to_double(pd[i]), fixp_to_double(total));
		if (stab < pd[i])
			return q->move[i];
		stab -= pd[i];
	}

	/* Tenuki. */
	assert(stab < double_to_fixp(pp->tenuki_prob));
	return pass;
}

#define FULLCHOOSE_ADD_TAGGED(code, tag)   do \
{ \
	mq_t q;  mq_init(&q); \
	do { code; } while(0); \
	for (int i = 0; i < q.moves; i++) \
		mtmq_add_nodup(&mq, q.move[i], tag); \
} while(0)


static coord_t
playout_moggy_fullchoose(playout_policy_t *p, playout_setup_t *s, board_t *b, enum stone to_play)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	mtmq_t mq;  mtmq_init(&mq);  /* Tagged queue */

	/* Ko fight check */
	if (pp->korate > 0 && !is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			mtmq_add_nodup(&mq, b->last_ko.coord, 1<<MQ_KO);
	}

	/* Local checks */
	if (!is_pass(last_move(b).coord)) {
		/* Local group in atari? */
		if (pp->lcapturerate > 0)
			FULLCHOOSE_ADD_TAGGED(local_atari_check(p, b, &last_move(b), &q), 1<<MQ_LATARI);

		/* Local group trying to escape ladder? */
		if (pp->ladderrate > 0)
			FULLCHOOSE_ADD_TAGGED(local_ladder_check(p, b, &last_move(b), &q), 1<<MQ_LADDER);

		/* Local group can be PUT in atari? */
		if (pp->atarirate > 0)
			FULLCHOOSE_ADD_TAGGED(local_2lib_check(p, b, &last_move(b), &q), 1<<MQ_L2LIB);

		/* Local group reduced some of our groups to 3 libs? */
		if (pp->nlibrate > 0)
			FULLCHOOSE_ADD_TAGGED(local_nlib_check(p, b, &last_move(b), &q), 1<<MQ_LNLIB);

		/* Some other semeai-ish shape checks */
		if (pp->eyefixrate > 0)
			FULLCHOOSE_ADD_TAGGED(eye_fix_check(p, b, &last_move(b), to_play, &q), 0); // XXX no tag

		/* Nakade check */
		if (pp->nakaderate > 0 && immediate_liberty_count(b, last_move(b).coord) > 0) {
			coord_t nakade = nakade_check(p, b, &last_move(b), to_play);
			if (!is_pass(nakade))
				mtmq_add_nodup(&mq, nakade, 1<<MQ_NAKADE);
		}

		/* Check for patterns we know */
		if (pp->patternrate > 0) {
			gmq_t q;  gmq_init(&q);
			move_t *last_move2 = (pp->pattern2 && last_move2(b).coord >= 0 ? &last_move2(b) : NULL);
			apply_pattern(p, b, &last_move(b), last_move2, &q);
			/* FIXME: Use the gammas. */
			for (int i = 0; i < q.moves; i++)
				mtmq_add_nodup(&mq, q.move[i], MQ_PAT3);
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > 0)
		FULLCHOOSE_ADD_TAGGED(global_atari_check(p, b, to_play, &q), 1<<MQ_GATARI);

#ifdef MOGGY_JOSEKI
	/* Joseki moves? */
	if (pp->josekirate > 0)
		FULLCHOOSE_ADD_TAGGED(joseki_check(p, b, to_play, &q), 1<<MQ_JOSEKI);
#endif

#if 0
	/* Average length of the queue is 1.4 move. */
	printf("MQL %d ", q.moves);
	for (int i = 0; i < q.moves; i++)
		printf("%s ", coord2sstr(q.move[i]));
	printf("\n");
#endif

	if (mq.moves > 0)
		return mq_tagged_choose(p, b, to_play, &mq);

	/* Fill board */
	if (pp->fillboardtries > 0) {
		coord_t c = fillboard_check(p, b);
		if (!is_pass(c))
			return c;
	}

	return pass;
}


#define permit_move(c)  playout_permit(p, b, c, m->color, random_move)

/* alt parameter tells permit if we just want a yes/no answer for this move
 * (alt=false) or we're ok with redirects if it doesn't pass (alt=true).
 * Every playout move must pass permit() before being played. When permit()
 * wants to suggest another move we need to validate this move as well, so
 * permit() needs to call permit() again on that move. This time alt will be
 * false though (we just want a yes/no answer) so it won't recurse again. */
static bool
playout_moggy_permit(playout_policy_t *p, board_t *b, move_t *m, bool alt, bool random_move)
{
	moggy_policy_t *pp = (moggy_policy_t*)p->data;
	moggy_state_t *ps = (moggy_state_t*)b->ps;

	/* The idea is simple for now - never allow bad self-atari moves.
	 * They suck in general, but this also permits us to actually
	 * handle seki in the playout stage. */

	int bad_selfatari = (pp->selfatarirate > fast_random(100) ? 
			     is_bad_selfatari(b, m->color, m->coord) :
			     is_really_bad_selfatari(b, m->color, m->coord));
	if (bad_selfatari) {
		if (DEBUGL(5))
			fprintf(stderr, "Moggy: Prohibiting self-atari %s %s\n", stone2str(m->color), coord2sstr(m->coord));
		if (alt && pp->selfatari_other) {
			ps->last_selfatari[m->color] = m->coord;
			/* Ok, try the other liberty of the atari'd group. */
			coord_t c = selfatari_cousin(b, m->color, m->coord, NULL);
			if (!permit_move(c)) return false;
			if (DEBUGL(5))
				fprintf(stderr, "Moggy: Redirecting to other lib %s\n", coord2sstr(c));
			m->coord = c;
			return true;
		}
		return false;
	}

	/* Check if we don't seem to be filling our eye. This should
	 * happen only for false eyes, but some of them are in fact
	 * real eyes with diagonal filled by a dead stone. Prefer
	 * to counter-capture in that case. */
	if (!alt || fast_random(100) >= pp->eyefillrate)
		goto eyefill_skip;

	{
	bool eyefill = board_is_eyelike(b, m->coord, m->color);
	/* If saving a group in atari don't interfere ! */
	if (eyefill && !board_get_atari_neighbor(b, m->coord, m->color)) {
		if (DEBUGL(5))  fprintf(stderr, "Moggy: %s filling eye, checking neighbors\n", coord2sstr(m->coord));
		foreach_diag_neighbor(b, m->coord) {
			if (board_at(b, c) != stone_other(m->color))
				continue;
			switch (board_group_info(b, group_at(b, c)).libs) {
			case 1: /* Capture! */
				c = board_group_info(b, group_at(b, c)).lib[0];
				if (!permit_move(c))
					break;
				if (DEBUGL(5))  fprintf(stderr, "Moggy: Redirecting to capture %s\n", coord2sstr(c));
				m->coord = c;
				return true;
			case 2: /* Try to switch to some 2-lib neighbor. */
				for (int i = 0; i < 2; i++) {
					coord_t l = board_group_info(b, group_at(b, c)).lib[i];
					if (!permit_move(l))
						continue;
					if (DEBUGL(5))  fprintf(stderr, "Moggy: Redirecting to %s atari\n", coord2sstr(l));
					m->coord = l;
					return true;
				}
				break;
			}
		} foreach_diag_neighbor_end;
	}
	}

eyefill_skip:
	/* Check special sekis moggy would break. */
	if (check_special_sekis(b, m)) {
		if (breaking_3_stone_seki(b, m->coord, m->color))
			return false;
		if (check_endgame_sekis(b, m, random_move) &&
		    breaking_false_eye_seki(b, m->coord, m->color))
			return false;
	}
	return true;
}

static void
playout_moggy_setboard(playout_policy_t *playout_policy, board_t *b)
{
	if (b->ps)
		return;
	moggy_state_t *ps = malloc2(moggy_state_t);
	ps->last_selfatari[S_BLACK] = ps->last_selfatari[S_WHITE] = 0;
	b->ps = ps;
}

playout_policy_t *
playout_moggy_init(char *arg, board_t *b)
{
	playout_policy_t *p = calloc2(1, playout_policy_t);
	moggy_policy_t *pp = calloc2(1, moggy_policy_t);
	p->data = pp;
	p->setboard = playout_moggy_setboard;
	p->setboard_randomok = true;
	p->choose = playout_moggy_seqchoose;
	p->permit = playout_moggy_permit;

	/* These settings are tuned for 19x19 play with several threads
	 * on reasonable time limits (i.e., rather large number of playouts).
	 * XXX: no 9x9 tuning has been done recently. */
	int rate = board_large(b) ? 80 : 90;

	pp->patternrate = pp->eyefixrate = 100;
	pp->lcapturerate = 90;
	pp->atarirate = pp->josekirate = -1U;
	pp->nakaderate = 80;
	pp->korate = 40; pp->koage = 3;
	pp->alwaysccaprate = 40;
	pp->eyefillrate = 60;
	pp->nlibrate = 25;

	/* selfatarirate is slightly special, since to avoid playing some
	 * silly move that stays on the board, it needs to block it many
	 * times during a simulation - we'd like that to happen in most
	 * simulations, so we try to use a very high selfatarirate.
	 * XXX: Perhaps it would be better to permanently ban moves in
	 * the current simulation after testing them once.
	 * XXX: We would expect the above to be the case, but since some
	 * unclear point, selfatari 95 -> 60 gives a +~50Elo boost against
	 * GNUGo.  This might be indicative of some bug, FIXME bisect? */
	pp->selfatarirate = 60;
	pp->selfatari_other = true;

	pp->pattern2 = true;

	pp->cap_stone_min = 2;
	pp->cap_stone_max = 15;
	pp->cap_stone_denom = 200;

	pp->atari_def_no_hopeless = !board_large(b);
	pp->atari_miaisafe = true;
	pp->nlib_count = 4;

	/* C is stupid. */
	double mq_prob_default[MQ_MAX] = { 0.0, };
	mq_prob_default[MQ_KO] = 6.0;
	mq_prob_default[MQ_NAKADE] = 5.5;
	mq_prob_default[MQ_LATARI] = 5.0;
	mq_prob_default[MQ_L2LIB] = 4.0;
	mq_prob_default[MQ_LNLIB] = 3.5;
	mq_prob_default[MQ_PAT3] = 3.0;
	mq_prob_default[MQ_GATARI] = 2.0;
	mq_prob_default[MQ_JOSEKI] = 1.0;
	memcpy(pp->mq_prob, mq_prob_default, sizeof(pp->mq_prob));

	/* Default 3x3 pattern gammas tuned on 15x15 with 500s/game on
	 * i7-3770 single thread using 40000 CLOP games. */
	double pat3_gammas_default[PAT3_N] = {
		0.52, 0.53, 0.32, 0.22, 0.37, 0.28, 0.21, 0.19, 0.82,
		0.12, 0.20, 0.11, 0.16, 0.57, 0.44
	};
	//memcpy(pp->pat3_gammas, pat3_gammas_default, sizeof(pp->pat3_gammas));
	for (int i = 0; i < PAT3_N; i++)
		pp->pat3_gammas[i] = double_to_fixp(pat3_gammas_default[i]);

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
			} else if (!strcasecmp(optname, "ladderrate") && optval) {
				/* Note that ladderrate is considered obsolete;
				 * it is ineffective and superseded by the
				 * prune_ladders prior. */
				pp->ladderrate = atoi(optval);
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				pp->atarirate = atoi(optval);
			} else if (!strcasecmp(optname, "nlibrate") && optval) {
				pp->nlibrate = atoi(optval);
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "selfatarirate") && optval) {
				pp->selfatarirate = atoi(optval);
			} else if (!strcasecmp(optname, "eyefillrate") && optval) {
				pp->eyefillrate = atoi(optval);
			} else if (!strcasecmp(optname, "korate") && optval) {
				pp->korate = atoi(optval);
			} else if (!strcasecmp(optname, "josekirate") && optval) {
				pp->josekirate = atoi(optval);
			} else if (!strcasecmp(optname, "nakaderate") && optval) {
				pp->nakaderate = atoi(optval);
			} else if (!strcasecmp(optname, "eyefixrate") && optval) {
				pp->eyefixrate = atoi(optval);
			} else if (!strcasecmp(optname, "alwaysccaprate") && optval) {
				pp->alwaysccaprate = atoi(optval);
			} else if (!strcasecmp(optname, "rate") && optval) {
				rate = atoi(optval);
			} else if (!strcasecmp(optname, "fillboardtries")) {
				pp->fillboardtries = atoi(optval);
			} else if (!strcasecmp(optname, "koage") && optval) {
				pp->koage = atoi(optval);
			} else if (!strcasecmp(optname, "pattern2")) {
				pp->pattern2 = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "selfatari_other")) {
				pp->selfatari_other = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "capcheckall")) {
				pp->capcheckall = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "cap_stone_min") && optval) {
				pp->cap_stone_min = atoi(optval);
			} else if (!strcasecmp(optname, "cap_stone_max") && optval) {
				pp->cap_stone_max = atoi(optval);
			} else if (!strcasecmp(optname, "cap_stone_denom") && optval) {
				pp->cap_stone_denom = atoi(optval);
			} else if (!strcasecmp(optname, "atari_miaisafe")) {
				pp->atari_miaisafe = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "atari_def_no_hopeless")) {
				pp->atari_def_no_hopeless = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "nlib_count") && optval) {
				pp->nlib_count = atoi(optval);
			} else if (!strcasecmp(optname, "middle_ladder")) {
				pp->middle_ladder = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "fullchoose")) {
				pp->fullchoose = true;
				p->choose = optval && *optval == '0' ? playout_moggy_seqchoose : playout_moggy_fullchoose;
			} else if (!strcasecmp(optname, "mqprob") && optval) {
				/* KO%LATARI%L2LIB%LNLIB%PAT3%GATARI%JOSEKI%NAKADE */
				for (int i = 0; *optval && i < MQ_MAX; i++) {
					pp->mq_prob[i] = atof(optval);
					optval += strcspn(optval, "%");
					if (*optval) optval++;
				}
			} else if (!strcasecmp(optname, "pat3gammas") && optval) {
				/* PAT3_N %-separated floating point values */
				for (int i = 0; *optval && i < PAT3_N; i++) {
					pp->pat3_gammas[i] = double_to_fixp(atof(optval));
					optval += strcspn(optval, "%");
					if (*optval) optval++;
				}
			} else if (!strcasecmp(optname, "tenukiprob") && optval) {
				pp->tenuki_prob = atof(optval);
			} else
				die("playout-moggy: Invalid policy argument %s or missing value\n", optname);
		}
	}
	if (pp->lcapturerate == -1U) pp->lcapturerate = rate;
	if (pp->atarirate == -1U) pp->atarirate = rate;
	if (pp->nlibrate == -1U) pp->nlibrate = rate;
	if (pp->capturerate == -1U) pp->capturerate = rate;
	if (pp->patternrate == -1U) pp->patternrate = rate;
	if (pp->selfatarirate == -1U) pp->selfatarirate = rate;
	if (pp->eyefillrate == -1U) pp->eyefillrate = rate;
	if (pp->korate == -1U) pp->korate = rate;
	if (pp->josekirate == -1U) pp->josekirate = rate;
	if (pp->ladderrate == -1U) pp->ladderrate = rate;
	if (pp->nakaderate == -1U) pp->nakaderate = rate;
	if (pp->eyefixrate == -1U) pp->eyefixrate = rate;
	if (pp->alwaysccaprate == -1U) pp->alwaysccaprate = rate;

	pattern3s_init(&pp->patterns, moggy_patterns_src, moggy_patterns_src_n);

	return p;
}
