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
	/* [board_size2(b)] array, whether to compute
	 * prior for the given value. */
	bool *consider;
};

/* Wins can be negative to give losses; passing 0 wins is undefined. */
static void add_prior_value(struct prior_map *map, coord_t c, int wins, int playouts);

void uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map);


static inline void
add_prior_value(struct prior_map *map, coord_t c, int wins, int playouts)
{
	map->prior[c].playouts += playouts;

	assert(wins != 0);
	int w = wins * map->parity;
	if (w < 0) w += playouts;
	assert(w >= 0);
	map->prior[c].wins += w;
}

#endif
