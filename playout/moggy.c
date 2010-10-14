/* Playout policy by stochastically applying a fixed set of decision
 * rules in given order - modelled after the intelligent playouts
 * in the Mogo engine. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "joseki/base.h"
#include "mq.h"
#include "pattern3.h"
#include "playout.h"
#include "playout/moggy.h"
#include "random.h"
#include "tactics/1lib.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"
#include "uct/prior.h"

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)

/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


/* Move queue tags: */
enum mq_tag {
	MQ_KO = 1,
	MQ_LATARI,
	MQ_L2LIB,
	MQ_PAT3,
	MQ_GATARI,
	MQ_JOSEKI,
	MQ_MAX
};


/* Note that the context can be shared by multiple threads! */

struct moggy_policy {
	unsigned int lcapturerate, atarirate, capturerate, patternrate, korate, josekirate;
	unsigned int selfatarirate, alwaysccaprate;
	unsigned int fillboardtries;
	int koage;
	/* Whether to look for patterns around second-to-last move. */
	bool pattern2;
	/* Whether, when self-atari attempt is detected, to play the other
	 * group's liberty if that is non-self-atari. */
	bool selfatari_other;
	/* Whether to always pick from moves capturing all groups in
	 * global_atari_check(). */
	bool capcheckall;

	struct joseki_dict *jdict;
	struct pattern3s patterns;

	/* Gamma values for queue tags - correspond to probabilities. */
	/* XXX: Tune. */
	double mq_prob[MQ_MAX], tenuki_prob;
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
	/* side pattern - block side cut */
	"OX?"
	"X.O"
	"###",
	/* side pattern - block side connection */
	"?X?"
	"x.O"
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

static inline bool
test_pattern3_here(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;
	/* Check if 3x3 pattern is matched by given move... */
	if (!pattern3_move_here(&pp->patterns, b, m))
		return false;
	/* ...and the move is not obviously stupid. */
	if (is_bad_selfatari(b, m->color, m->coord))
		return false;
	/* Ladder moves are stupid. */
	group_t atari_neighbor = board_get_atari_neighbor(b, m->coord, m->color);
	if (atari_neighbor && is_ladder(b, m->coord, atari_neighbor))
		return false;
	return true;
}

static void
apply_pattern_here(struct playout_policy *p, struct board *b, coord_t c, enum stone color, struct move_queue *q)
{
	struct move m2 = { .coord = c, .color = color };
	if (board_is_valid_move(b, &m2) && test_pattern3_here(p, b, &m2))
		mq_add(q, c, 1<<MQ_PAT3);
}

/* Check if we match any pattern around given move (with the other color to play). */
static void
apply_pattern(struct playout_policy *p, struct board *b, struct move *m, struct move *mm, struct move_queue *q)
{
	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return;

	foreach_8neighbor(b, m->coord) {
		apply_pattern_here(p, b, c, stone_other(m->color), q);
	} foreach_8neighbor_end;

	if (mm) { /* Second move for pattern searching */
		foreach_8neighbor(b, mm->coord) {
			if (coord_is_8adjecent(m->coord, c, b))
				continue;
			apply_pattern_here(p, b, c, stone_other(m->color), q);
		} foreach_8neighbor_end;
	}

	if (PLDEBUGL(5))
		mq_print(q, b, "Pattern");
}


static void
joseki_check(struct playout_policy *p, struct board *b, enum stone to_play, struct move_queue *q)
{
	struct moggy_policy *pp = p->data;
	if (!pp->jdict)
		return;

	for (int i = 0; i < 4; i++) {
		hash_t h = b->qhash[i] & joseki_hash_mask;
		coord_t *cc = pp->jdict->patterns[h].moves[to_play];
		if (!cc) continue;
		for (; !is_pass(*cc); cc++) {
			if (coord_quadrant(*cc, b) != i)
				continue;
			mq_add(q, *cc, 1<<MQ_JOSEKI);
		}
	}

	if (q->moves > 0 && PLDEBUGL(5))
		mq_print(q, b, "Joseki");
}

static void
global_atari_check(struct playout_policy *p, struct board *b, enum stone to_play, struct move_queue *q)
{
	if (b->clen == 0)
		return;

	struct moggy_policy *pp = p->data;
	if (pp->capcheckall) {
		for (int g = 0; g < b->clen; g++)
			group_atari_check(pp->alwaysccaprate, b, group_at(b, group_base(b->c[g])), to_play, q, NULL, 1<<MQ_GATARI);
		if (PLDEBUGL(5))
			mq_print(q, b, "Global atari");
		return;
	}

	int g_base = fast_random(b->clen);
	for (int g = g_base; g < b->clen; g++) {
		group_atari_check(pp->alwaysccaprate, b, group_at(b, group_base(b->c[g])), to_play, q, NULL, 1<<MQ_GATARI);
		if (q->moves > 0) {
			/* XXX: Try carrying on. */
			if (PLDEBUGL(5))
				mq_print(q, b, "Global atari");
			return;
		}
	}
	for (int g = 0; g < g_base; g++) {
		group_atari_check(pp->alwaysccaprate, b, group_at(b, group_base(b->c[g])), to_play, q, NULL, 1<<MQ_GATARI);
		if (q->moves > 0) {
			/* XXX: Try carrying on. */
			if (PLDEBUGL(5))
				mq_print(q, b, "Global atari");
			return;
		}
	}
	return;
}

static void
local_atari_check(struct playout_policy *p, struct board *b, struct move *m, struct move_queue *q)
{
	struct moggy_policy *pp = p->data;

	/* Did the opponent play a self-atari? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 1) {
		group_atari_check(pp->alwaysccaprate, b, group_at(b, m->coord), stone_other(m->color), q, NULL, 1<<MQ_LATARI);
	}

	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;
		group_atari_check(pp->alwaysccaprate, b, g, stone_other(m->color), q, NULL, 1<<MQ_LATARI);
	});

	if (PLDEBUGL(5))
		mq_print(q, b, "Local atari");
}

static bool
miai_2lib(struct board *b, group_t group, enum stone color)
{
	bool can_connect = false, can_pull_out = false;
	/* We have miai if we can either connect on both libs,
	 * or connect on one lib and escape on another. (Just
	 * having two escape routes can be risky.) We must make
	 * sure that we don't consider following as miai:
	 * X X X O
	 * X . . O
	 * O O X O - left dot would be pull-out, right dot connect */
	foreach_neighbor(b, board_group_info(b, group).lib[0], {
		enum stone cc = board_at(b, c);
		if (cc == S_NONE && cc != board_at(b, board_group_info(b, group).lib[1])) {
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
		if (c == board_group_info(b, group).lib[0])
			continue;
		if (cc == S_NONE && can_connect) {
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
check_group_atari(struct board *b, group_t group, enum stone owner,
		  enum stone to_play, struct move_queue *q, int tag)
{
	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, group).lib[i];
		assert(board_at(b, lib) == S_NONE);
		if (!board_is_valid_play(b, to_play, lib))
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
		if (neighbor_count_at(b, lib, stone_other(owner)) + immediate_liberty_count(b, lib) < 2)
			continue;
#endif

		/* If the move is too "lumpy", do not play it:
		 *
		 * #######
		 * ..O.X.X <- always play the left one!
		 * OXXXXXX */
		if (neighbor_count_at(b, lib, stone_other(owner)) + neighbor_count_at(b, lib, S_OFFBOARD) == 3)
			continue;

#ifdef NO_DOOMED_GROUPS
		/* If the owner can't play at the spot, we don't want
		 * to bother either. */
		if (is_bad_selfatari(b, owner, lib))
			continue;
#endif

		/* Of course we don't want to play bad selfatari
		 * ourselves, if we are the attacker... */
		if (
#ifdef NO_DOOMED_GROUPS
		    to_play != owner &&
#endif
		    is_bad_selfatari(b, to_play, lib))
			continue;

		/* Tasty! Crispy! Good! */
		mq_add(q, lib, tag);
		mq_nodup(q);
	}
}

static void
group_2lib_check(struct board *b, group_t group, enum stone to_play, struct move_queue *q, int tag)
{
	enum stone color = board_at(b, group_base(group));
	assert(color != S_OFFBOARD && color != S_NONE);

	if (DEBUGL(5))
		fprintf(stderr, "[%s] 2lib check of color %d\n",
			coord2sstr(group, b), color);

	/* Do not try to atari groups that cannot be harmed. */
	if (miai_2lib(b, group, color))
		return;

	check_group_atari(b, group, color, to_play, q, tag);

	/* Can we counter-atari another group, if we are the defender? */
	if (to_play != color)
		return;
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != stone_other(color))
				continue;
			group_t g2 = group_at(b, c);
			if (board_group_info(b, g2).libs == 1) {
				/* We can capture a neighbor. */
				mq_add(q, board_group_info(b, g2).lib[0], tag);
				mq_nodup(q);
				continue;
			}
			if (board_group_info(b, g2).libs != 2)
				continue;
			check_group_atari(b, g2, color, to_play, q, tag);
		});
	} foreach_in_group_end;
}

static void
local_2lib_check(struct playout_policy *p, struct board *b, struct move *m, struct move_queue *q)
{
	/* Does the opponent have just two liberties? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 2) {
		group_2lib_check(b, group_at(b, m->coord), stone_other(m->color), q, 1<<MQ_L2LIB);
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
		if (!g || board_group_info(b, g).libs != 2)
			continue;
		group_2lib_check(b, g, stone_other(m->color), q, 1<<MQ_L2LIB);
	});

	if (PLDEBUGL(5))
		mq_print(q, b, "Local 2lib");
}

coord_t
fillboard_check(struct playout_policy *p, struct board *b)
{
	struct moggy_policy *pp = p->data;
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
next_try:;
	}
	return pass;
}

coord_t
playout_moggy_partchoose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct moggy_policy *pp = p->data;

	if (PLDEBUGL(5))
		board_print(b, stderr);

	/* Ko fight check */
	if (!is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage
	    && pp->korate > fast_random(100)) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			return b->last_ko.coord;
	}

	/* Local checks */
	if (!is_pass(b->last_move.coord)) {
		/* Local group in atari? */
		if (pp->lcapturerate > fast_random(100)) {
			struct move_queue q;  q.moves = 0;
			local_atari_check(p, b, &b->last_move, &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Local group can be PUT in atari? */
		if (pp->atarirate > fast_random(100)) {
			struct move_queue q; q.moves = 0;
			local_2lib_check(p, b, &b->last_move, &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}

		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			struct move_queue q; q.moves = 0;
			apply_pattern(p, b, &b->last_move,
			                  pp->pattern2 && b->last_move2.coord >= 0 ? &b->last_move2 : NULL,
					  &q);
			if (q.moves > 0)
				return mq_pick(&q);
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > fast_random(100)) {
		struct move_queue q; q.moves = 0;
		global_atari_check(p, b, to_play, &q);
		if (q.moves > 0)
			return mq_pick(&q);
	}

	/* Joseki moves? */
	if (pp->josekirate > fast_random(100)) {
		struct move_queue q; q.moves = 0;
		joseki_check(p, b, to_play, &q);
		if (q.moves > 0)
			return mq_pick(&q);
	}

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
coord_t
mq_tagged_choose(struct playout_policy *p, struct board *b, enum stone to_play, struct move_queue *q)
{
	struct moggy_policy *pp = p->data;

	/* First, merge all entries for a move. */
	/* We use a naive O(N^2) since the average length of the queue
	 * is about 1.4. */
	for (unsigned int i = 0; i < q->moves; i++) {
		for (unsigned int j = i + 1; j < q->moves; j++) {
			if (q->move[i] != q->move[j])
				continue;
			q->tag[i] |= q->tag[j];
			q->moves--;
			q->tag[j] = q->tag[q->moves];
			q->move[j] = q->move[q->moves];
		}
	}

	/* Now, construct a probdist. */
	fixp_t total = 0;
	fixp_t pd[q->moves];
	for (unsigned int i = 0; i < q->moves; i++) {
		double val = 1.0;
		assert(q->tag[i] != 0);
		for (int j = 1; j < MQ_MAX; j++)
			if (q->tag[i] & (1<<j)) {
				//fprintf(stderr, "%s(%x) %d %f *= %f\n", coord2sstr(q->move[i], b), q->tag[i], j, val, pp->mq_prob[j]);
				val *= pp->mq_prob[j];
			}
		pd[i] = double_to_fixp(val);
		total += pd[i];
	}
	total += double_to_fixp(pp->tenuki_prob);

	/* Finally, pick a move! */
	fixp_t stab = fast_irandom(total);
	for (unsigned int i = 0; i < q->moves; i++) {
		//fprintf(stderr, "%s(%x) %f (%f/%f)\n", coord2sstr(q->move[i], b), q->tag[i], fixp_to_double(stab), fixp_to_double(pd[i]), fixp_to_double(total));
		if (stab < pd[i])
			return q->move[i];
		stab -= pd[i];
	}

	/* Tenuki. */
	assert(stab < double_to_fixp(pp->tenuki_prob));
	return pass;
}

coord_t
playout_moggy_fullchoose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct moggy_policy *pp = p->data;
	struct move_queue q; q.moves = 0;

	if (PLDEBUGL(5))
		board_print(b, stderr);

	/* Ko fight check */
	if (!is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage
	    && pp->korate > fast_random(100)) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			mq_add(&q, b->last_ko.coord, 1<<MQ_KO);
	}

	/* Local checks */
	if (!is_pass(b->last_move.coord)) {
		/* Local group in atari? */
		if (pp->lcapturerate > fast_random(100)) {
			local_atari_check(p, b, &b->last_move, &q);
		}

		/* Local group can be PUT in atari? */
		if (pp->atarirate > fast_random(100)) {
			local_2lib_check(p, b, &b->last_move, &q);
		}

		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			apply_pattern(p, b, &b->last_move,
			                  pp->pattern2 && b->last_move2.coord >= 0 ? &b->last_move2 : NULL,
					  &q);
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > fast_random(100)) {
		global_atari_check(p, b, to_play, &q);
	}

	/* Joseki moves? */
	if (pp->josekirate > fast_random(100)) {
		joseki_check(p, b, to_play, &q);
	}

#if 0
	/* Average length of the queue is 1.4 move. */
	printf("MQL %d ", q.moves);
	for (unsigned int i = 0; i < q.moves; i++)
		printf("%s ", coord2sstr(q.move[i], b));
	printf("\n");
#endif

	if (q.moves > 0)
		return mq_tagged_choose(p, b, to_play, &q);

	/* Fill board */
	if (pp->fillboardtries > 0) {
		coord_t c = fillboard_check(p, b);
		if (!is_pass(c))
			return c;
	}

	return pass;
}


static coord_t
selfatari_cousin(struct board *b, enum stone color, coord_t coord)
{
	group_t groups[4]; int groups_n = 0;
	foreach_neighbor(b, coord, {
		enum stone s = board_at(b, c);
		if (s != color) continue;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs == 2)
			groups[groups_n++] = g;
	});

	if (!groups_n)
		return pass;
	group_t group = groups[fast_random(groups_n)];

	coord_t lib2 = board_group_other_lib(b, group, coord);
	if (is_bad_selfatari(b, color, lib2))
		return pass;
	return lib2;
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
		group_2lib_check(b, g, map->to_play, &q, 0);
		while (q.moves--) {
			coord_t coord = q.move[q.moves];
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: 2lib %s\n", coord2sstr(coord, b));
			int assess = games / 2;
			add_prior_value(map, coord, 1, assess);
		}
		return;
	}

	/* This group, sir, is in atari! */

	coord_t ladder = pass;
	group_atari_check(pp->alwaysccaprate, b, g, map->to_play, &q, &ladder, 0);
	while (q.moves--) {
		coord_t coord = q.move[q.moves];

		/* _Never_ play here if this move plays out
		 * a caught ladder. */
		if (coord == ladder && !board_playing_ko_threat(b)) {
			/* Note that the opposite is not guarded against;
			 * we do not advise against capturing a laddered
			 * group (but we don't encourage it either). Such
			 * a move can simplify tactical situations if we
			 * can afford it. */
			if (map->to_play != board_at(b, g))
				continue;
			/* FIXME: We give the malus even if this move
			 * captures another group. */
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: ladder %s\n", coord2sstr(coord, b));
			add_prior_value(map, coord, 0, games);
			continue;
		}

		if (!pp->capturerate && !pp->lcapturerate)
			continue;

		if (PLDEBUGL(5))
			fprintf(stderr, "1.0: atari %s\n", coord2sstr(coord, b));
		int assess = games * 2;
		add_prior_value(map, coord, 1, assess);
	}
}

void
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
		if (!board_playing_ko_threat(b) && is_bad_selfatari(b, map->to_play, coord)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: self-atari\n");
			add_prior_value(map, coord, 0, games);
			if (!pp->selfatari_other)
				return;
			/* If we can play on the other liberty of the
			 * endangered group, do! */
			coord = selfatari_cousin(b, map->to_play, coord);
			if (is_pass(coord))
				return;
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: self-atari redirect %s\n", coord2sstr(coord, b));
			add_prior_value(map, coord, 1.0, games);
			return;
		}
	}

	/* Pattern check */
	if (pp->patternrate) {
		struct move m = { .color = map->to_play, .coord = coord };
		if (test_pattern3_here(p, b, &m)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: pattern\n");
			add_prior_value(map, coord, 1, games);
		}
	}

	return;
}

void
playout_moggy_assess(struct playout_policy *p, struct prior_map *map, int games)
{
	struct moggy_policy *pp = p->data;

	/* First, go through all endangered groups. */
	for (group_t g = 1; g < board_size2(map->b); g++)
		if (group_at(map->b, g) == g)
			playout_moggy_assess_group(p, map, g, games);

	/* Then, assess individual moves. */
	if (!pp->patternrate && !pp->selfatarirate)
		return;
	foreach_free_point(map->b) {
		if (map->consider[c])
			playout_moggy_assess_one(p, map, c, games);
	} foreach_free_point_end;
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
	if (selfatari) {
		if (PLDEBUGL(5))
			fprintf(stderr, "__ Prohibiting self-atari %s %s\n",
				stone2str(m->color), coord2sstr(m->coord, b));
		if (pp->selfatari_other) {
			/* Ok, try the other liberty of the atari'd group. */
			coord_t c = selfatari_cousin(b, m->color, m->coord);
			if (is_pass(c)) return false;
			if (PLDEBUGL(5))
				fprintf(stderr, "___ Redirecting to other lib %s\n",
					coord2sstr(c, b));
			m->coord = c;
			return true;
		}
		return false;
	}
	return true;
}


struct playout_policy *
playout_moggy_init(char *arg, struct board *b, struct joseki_dict *jdict)
{
	struct playout_policy *p = calloc2(1, sizeof(*p));
	struct moggy_policy *pp = calloc2(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_moggy_partchoose;
	p->assess = playout_moggy_assess;
	p->permit = playout_moggy_permit;

	pp->jdict = jdict;

	int rate = 90;

	pp->lcapturerate = pp->atarirate = pp->capturerate = pp->patternrate
			= pp->selfatarirate = pp->josekirate = -1U;
	pp->korate = 0; pp->koage = 4;
	pp->alwaysccaprate = 0;
	pp->selfatari_other = true;

	/* C is stupid. */
	double mq_prob_default[MQ_MAX] = {
		[MQ_KO] = 6.0,
		[MQ_LATARI] = 5.0,
		[MQ_L2LIB] = 4.0,
		[MQ_PAT3] = 3.0,
		[MQ_GATARI] = 2.0,
		[MQ_JOSEKI] = 1.0,
	};
	memcpy(pp->mq_prob, mq_prob_default, sizeof(pp->mq_prob));

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug") && optval) {
				p->debug_level = atoi(optval);
			} else if (!strcasecmp(optname, "lcapturerate") && optval) {
				pp->lcapturerate = atoi(optval);
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				pp->atarirate = atoi(optval);
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "selfatarirate") && optval) {
				pp->selfatarirate = atoi(optval);
			} else if (!strcasecmp(optname, "korate") && optval) {
				pp->korate = atoi(optval);
			} else if (!strcasecmp(optname, "josekirate") && optval) {
				pp->josekirate = atoi(optval);
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
			} else if (!strcasecmp(optname, "fullchoose")) {
				p->choose = optval && *optval == '0' ? playout_moggy_partchoose : playout_moggy_fullchoose;
			} else if (!strcasecmp(optname, "mqprob") && optval) {
				/* KO%LATARI%L2LIB%PAT3%GATARI%JOSEKI */
				for (int i = 1; *optval && i < MQ_MAX; i++, optval += strcspn(optval, "%")) {
					optval++;
					pp->mq_prob[i] = atof(optval);
				}
			} else if (!strcasecmp(optname, "tenukiprob") && optval) {
				pp->tenuki_prob = atof(optval);
			} else {
				fprintf(stderr, "playout-moggy: Invalid policy argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}
	if (pp->lcapturerate == -1U) pp->lcapturerate = rate;
	if (pp->atarirate == -1U) pp->atarirate = rate;
	if (pp->capturerate == -1U) pp->capturerate = rate;
	if (pp->patternrate == -1U) pp->patternrate = rate;
	if (pp->selfatarirate == -1U) pp->selfatarirate = rate;
	if (pp->korate == -1U) pp->korate = rate;
	if (pp->josekirate == -1U) pp->josekirate = rate;
	if (pp->alwaysccaprate == -1U) pp->alwaysccaprate = rate;

	pattern3s_init(&pp->patterns, moggy_patterns_src, moggy_patterns_src_n);

	return p;
}
