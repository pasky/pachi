#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "libmap.h"
#include "move.h"
#include "tactics/util.h"


hash_t
group_to_libmap(struct board *b, group_t group)
{
	hash_t h = 0;
#define hbits (sizeof(hash_t)*CHAR_BIT)

	enum stone color = board_at(b, group);
	struct group *gi = &board_group_info(b, group);
	int libs = gi->libs < GROUP_REFILL_LIBS ? gi->libs : GROUP_REFILL_LIBS;

	for (int i = 0; i < libs; i++) {
		hash_t hlib = hash_at(b, gi->lib[i], color);
		/* Rotate the hash based on prospects of the liberty. */
		int p = immediate_liberty_count(b, gi->lib[i]) +
		          4 * neighbor_count_at(b, gi->lib[i], color);
		hlib = (hlib << p) | ((hlib >> (hbits - p)) & ((1<<p) - 1));
		/* Add to hash. */
		h ^= hlib;
	}

	return h;
}


struct libmap_config libmap_config = {
	.pick_mode = LMP_THRESHOLD,
	.pick_threshold = 0.7,
	.pick_epsilon = 10,

	.explore_p = 0.2,
	.prior = { .value = 0.5, .playouts = 1 },

	.mq_merge_groups = true,
	.counterattack = LMC_DEFENSE | LMC_ATTACK | LMC_DEFENSE_ATTACK,
	.eval = LME_LVALUE,
};

void
libmap_setup(char *arg)
{
	if (!arg)
		return;

	char *optspec, *next = arg;
	while (*next) {
		optspec = next;
		next += strcspn(next, ":");
		if (*next) { *next++ = 0; } else { *next = 0; }

		char *optname = optspec;
		char *optval = strchr(optspec, '=');
		if (optval) *optval++ = 0;

		if (!strcasecmp(optname, "pick_mode") && optval) {
			if (!strcasecmp(optval, "threshold")) {
				libmap_config.pick_mode = LMP_THRESHOLD;
			} else if (!strcasecmp(optval, "ucb")) {
				libmap_config.pick_mode = LMP_UCB;
			} else {
				fprintf(stderr, "Invalid libmap:pick_mode value %s\n", optval);
				exit(1);
			}

		} else if (!strcasecmp(optname, "pick_threshold") && optval) {
			libmap_config.pick_threshold = atof(optval);
		} else if (!strcasecmp(optname, "pick_epsilon") && optval) {
			libmap_config.pick_epsilon = atoi(optval);
		} else if (!strcasecmp(optname, "avoid_bad")) {
			libmap_config.avoid_bad = !optval || atoi(optval);

		} else if (!strcasecmp(optname, "explore_p") && optval) {
			libmap_config.explore_p = atof(optval);
		} else if (!strcasecmp(optname, "prior") && optval && strchr(optval, 'x')) {
			libmap_config.prior.value = atof(optval);
			optval += strcspn(optval, "x") + 1;
			libmap_config.prior.playouts = atoi(optval);

		} else if (!strcasecmp(optname, "mq_merge_groups")) {
			libmap_config.mq_merge_groups = !optval || atoi(optval);
		} else if (!strcasecmp(optname, "counterattack") && optval) {
			/* Combination of letters d, a, x (both), these kinds
			 * of hashes are going to be recorded. */
			/* Note that using multiple letters makes no sense
			 * if mq_merge_groups is set. */
			libmap_config.counterattack = 0;
			if (strchr(optval, 'd'))
				libmap_config.counterattack |= LMC_DEFENSE;
			if (strchr(optval, 'a'))
				libmap_config.counterattack |= LMC_ATTACK;
			if (strchr(optval, 'x'))
				libmap_config.counterattack |= LMC_DEFENSE_ATTACK;
		} else if (!strcasecmp(optname, "eval") && optval) {
			if (!strcasecmp(optval, "local")) {
				libmap_config.eval = LME_LOCAL;
			} else if (!strcasecmp(optval, "lvalue")) {
				libmap_config.eval = LME_LVALUE;
			} else if (!strcasecmp(optval, "global")) {
				libmap_config.eval = LME_GLOBAL;
			} else {
				fprintf(stderr, "Invalid libmap:eval value %s\n", optval);
				exit(1);
			}
		} else if (!strcasecmp(optname, "tenuki")) {
			libmap_config.tenuki = !optval || atoi(optval);
		} else {
			fprintf(stderr, "Invalid libmap argument %s or missing value\n", optname);
			exit(1);
		}
	}
}


struct libmap_hash *
libmap_init(struct board *b)
{
	struct libmap_hash *lm = calloc2(1, sizeof(*lm));
	lm->b = b;
	b->libmap = lm;
	lm->refcount = 1;
	return lm;
}

void
libmap_put(struct libmap_hash *lm)
{
	if (__sync_sub_and_fetch(&lm->refcount, 1) > 0)
		return;
	free(lm);
}

void
libmap_queue_process(struct libmap_hash *lm, struct board *b, enum stone winner)
{
	assert(lm->queue.mq.moves <= MQL);
	for (unsigned int i = 0; i < lm->queue.mq.moves; i++) {
		struct libmap_group *g = &lm->queue.group[i];
		struct move m = { .coord = lm->queue.mq.move[i], .color = lm->queue.color[i] };
		floating_t val;
		if (libmap_config.eval == LME_LOCAL || libmap_config.eval == LME_LVALUE) {
			val = board_local_value(libmap_config.eval == LME_LVALUE, b, g->group, g->goal);

		} else { assert(libmap_config.eval == LME_GLOBAL);
			val = winner == g->goal ? 1.0 : 0.0;
		}
		libmap_add_result(lm, g->hash, m, val, 1);
	}
	lm->queue.mq.moves = 0;
}

void
libmap_add_result(struct libmap_hash *lm, hash_t hash, struct move move,
                  floating_t result, int playouts)
{
	/* If hash line is full, replacement strategy is naive - pick the
	 * move with minimum move[0].stats.playouts; resolve each tie
	 * randomly. */
	unsigned int min_playouts = INT_MAX; hash_t min_hash = hash;
	hash_t ih;
	for (ih = hash; lm->hash[ih & libmap_hash_mask].hash != hash; ih++) {
		// fprintf(stderr, "%"PRIhash": check %"PRIhash" (%d)\n", hash & libmap_hash_mask, ih & libmap_hash_mask, lm->hash[ih & libmap_hash_mask].moves);
		if (lm->hash[ih & libmap_hash_mask].moves == 0) {
			lm->hash[ih & libmap_hash_mask].hash = hash;
			break;
		}
		if (ih >= hash + libmap_hash_maxline) {
			/* Snatch the least used bucket. */
			ih = min_hash;
			// fprintf(stderr, "clear %"PRIhash"\n", ih & libmap_hash_mask);
			memset(&lm->hash[ih & libmap_hash_mask], 0, sizeof(lm->hash[0]));
			lm->hash[ih & libmap_hash_mask].hash = hash;
			break;
		}

		/* Keep track of least used bucket. */
		assert(lm->hash[ih & libmap_hash_mask].moves > 0);
		unsigned int hp = lm->hash[ih & libmap_hash_mask].move[0].stats.playouts;
		if (hp < min_playouts || (hp == min_playouts && fast_random(2))) {
			min_playouts = hp;
			min_hash = ih;
		}
	}

	// fprintf(stderr, "%"PRIhash": use %"PRIhash" (%d)\n", hash & libmap_hash_mask, ih & libmap_hash_mask, lm->hash[ih & libmap_hash_mask].moves);
	struct libmap_context *lc = &lm->hash[ih & libmap_hash_mask];
	for (int i = 0; i < lc->moves; i++) {
		if (lc->move[i].move.coord == move.coord
		    && lc->move[i].move.color == move.color) {
			stats_add_result(&lc->move[i].stats, result, playouts);
			return;
		}
	}

	int moves = lc->moves; // to preserve atomicity
	if (moves >= GROUP_REFILL_LIBS) {
		if (DEBUGL(5))
			fprintf(stderr, "(%s) too many libs\n", coord2sstr(move.coord, lm->b));
		return;
	}
	lc->move[moves].move = move;
	stats_add_result(&lc->move[moves].stats, result, playouts);
	lc->moves = ++moves;
}

struct move_stats
libmap_board_move_stats(struct libmap_hash *lm, struct board *b, struct move move)
{
	struct move_stats tot = { .playouts = 0, .value = 0 };
	if (is_pass(move.coord))
		return tot;
	assert(board_at(b, move.coord) != S_OFFBOARD);

	neighboring_groups_list(b, board_at(b, c) == S_BLACK || board_at(b, c) == S_WHITE,
			move.coord, groups, groups_n);
	for (int i = 0; i < groups_n; i++) {
		hash_t hash = group_to_libmap(b, groups[i]);
		struct move_stats *lp = libmap_move_stats(b->libmap, hash, move);
		if (!lp) continue;
		stats_merge(&tot, lp);
	}

	return tot;
}
