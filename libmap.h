#ifndef PACHI_LIBMAP_H
#define PACHI_LIBMAP_H

/* "Liberty map" - description of a particular liberty structure of a group.
 * The idea is that we can use this as a hash index to track local tactical
 * effectivity of various moves within the particular liberty structure
 * context. */

#include <assert.h>
#include <stdbool.h>

#include "board.h"
#include "mq.h"
#include "stats.h"

#define LM_DEBUG if (0)


/* Computation and representation of the libmap hash. */

hash_t group_to_libmap(struct board *b, group_t group);


/* Set of moves ("libmap context") grouped by libmap, with some statistics. */
/* Hash structure storing info about move effectivity. */

struct libmap_move {
	struct move move;
	struct move_stats stats;
};

struct libmap_context {
	hash_t hash;
	int visits;
	/* We add moves in multiple threads. But at most, on conflict we will
	 * end up with tiny amount of misappropriated playouts. */
	int moves;
	struct libmap_move move[GROUP_REFILL_LIBS];
};

#endif
