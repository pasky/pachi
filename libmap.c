#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "libmap.h"
#include "move.h"


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


struct libmap_hash *
libmap_init(struct board *b)
{
	struct libmap_hash *lm = calloc(1, sizeof(*lm));
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
libmap_queue_process(struct libmap_hash *lm, struct board *b)
{
	assert(lm->queue.mq.moves <= MQL);
	for (unsigned int i = 0; i < lm->queue.mq.moves; i++) {
		struct libmap_group *g = &lm->queue.group[i];
		struct move m = { .coord = lm->queue.mq.move[i], .color = lm->queue.color[i] };
		floating_t val = board_at(b, g->group) == g->goal ? 1.0 : 0.0;
		libmap_add_result(lm, g->hash, m, val, 1);
	}
	lm->queue.mq.moves = 0;
}

void
libmap_add_result(struct libmap_hash *lm, hash_t hash, struct move move,
                  floating_t result, int playouts)
{
	while (lm->hash[hash & libmap_hash_mask].hash != hash) {
		if (lm->hash[hash & libmap_hash_mask].moves == 0) {
			lm->hash[hash & libmap_hash_mask].hash = hash;
			break;
		}
		hash++;
	}
	struct libmap_context *lc = &lm->hash[hash & libmap_hash_mask];
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
	lc->moves = ++moves;
	stats_add_result(&lc->move[moves].stats, result, playouts);
}
