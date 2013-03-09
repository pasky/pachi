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
