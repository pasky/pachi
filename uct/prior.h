#ifndef PACHI_UCT_PRIOR_H
#define PACHI_UCT_PRIOR_H

#include "move.h"
#include "uct/tree.h"

struct tree;
struct tree_node;
struct uct;
struct board;

/* Applying heuristic values to the tree nodes, skewing the reading in
 * most interesting directions. */

struct uct_prior {
	/* Equivalent experience for prior knowledge. MoGo paper recommends
	 * 50 playouts per source; in practice, esp. with RAVE, about 6
	 * playouts per source seems best. */
	int eqex;
	int even_eqex, policy_eqex, b19_eqex, eye_eqex, ko_eqex, plugin_eqex;
	int joseki_eqex, joseki_eqex_far, pattern_eqex, dcnn_eqex;
	int cfgdn; int *cfgd_eqex;
	bool prune_ladders;
};

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
	/* [board_size2(b)] array from cfg_distances() */
	int *distances;
};

/* @value is the value, @playouts is its weight. */
static void add_prior_value(struct prior_map *map, coord_t c, floating_t value, int playouts);

void uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map);

struct uct_prior *uct_prior_init(char *arg, struct board *b, struct uct *u);
void uct_prior_done(struct uct_prior *p);


static inline void
add_prior_value(struct prior_map *map, coord_t c, floating_t value, int playouts)
{
	floating_t v = map->parity > 0 ? value : 1 - value;
	/* We don't need atomicity: */
	struct move_stats s = { .playouts = playouts, .value = v };
	stats_merge(&map->prior[c], &s);
}

/* Display node's priors best moves */
void print_node_prior_best_moves(struct board *b, struct tree_node *parent);
void get_node_prior_best_moves(struct tree_node *parent, coord_t *best_c, float *best_r, int nbest);

#endif
