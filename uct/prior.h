#ifndef ZZGO_UCT_PRIOR_H
#define ZZGO_UCT_PRIOR_H

#include "move.h"
#include "uct/tree.h"

struct tree;
struct tree_node;
struct uct;
struct board;

struct prior_map {
	struct board *b;
	enum stone to_play;
	int parity;
	/* [board_size2(b)] array, move_stats are the prior
	 * values to be assigned to individual moves;
	 * move_stats.value is not updated. */
	struct move_stats *prior;
};

void uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map);

#endif
