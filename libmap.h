#ifndef PACHI_LIBMAP_H
#define PACHI_LIBMAP_H

/* "Liberty map" - description of a particular liberty structure of a group.
 * The idea is that we can track local tactical effectivity of various moves
 * within the particular liberty structure context. */

#include "board.h"
#include "stats.h"

hash_t group_to_libmap(struct board *b, group_t group);


/* Tactical application - hash structure storing info about move effectivity. */

struct libmap_move {
	struct move move;
	struct move_stats stats;
};

struct libmap_context {
	hash_t hash;
	/* We add moves in multiple threads. But at most, we will end up
	 * with tiny misappropriated playouts in case of conflict. */
	int moves;
	struct libmap_move move[GROUP_REFILL_LIBS];
	
};

struct libmap_hash {
	struct board *b;
	int refcount;

#define libmap_hash_bits 17
#define libmap_hash_size (1 << libmap_hash_bits)
#define libmap_hash_mask (libmap_hash_size - 1)
	struct libmap_context hash[libmap_hash_bits];
};

struct libmap_hash *libmap_init(struct board *b);
void libmap_put(struct libmap_hash *lm);
void libmap_add_result(struct libmap_hash *lm, hash_t hash, struct move move, floating_t result, int playouts);

static struct move_stats *libmap_move_stats(struct libmap_hash *lm, hash_t hash, struct move move);


static inline struct move_stats *
libmap_move_stats(struct libmap_hash *lm, hash_t hash, struct move move)
{
	while (lm->hash[hash & libmap_hash_mask].hash != hash) {
		if (lm->hash[hash & libmap_hash_mask].moves == 0)
			return NULL;
		hash++;
	}
	struct libmap_context *lc = &lm->hash[hash & libmap_hash_mask];
	for (int i = 0; i < lc->moves; i++) {
		if (lc->move[i].move.coord == move.coord
		    && lc->move[i].move.color == move.color)
			return &lc->move[i].stats;
	}
	return NULL;
}

#endif
