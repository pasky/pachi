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

typedef struct {
	/* Equivalent experience for prior knowledge. MoGo paper recommends
	 * 50 playouts per source; in practice, esp. with RAVE, about 6
	 * playouts per source seems best. */
	int eqex;
	int even_eqex, policy_eqex, b19_eqex, eye_eqex, ko_eqex, plugin_eqex;
	int joseki_eqex, joseki_eqex_far, pattern_eqex, dcnn_eqex;
	int cfgdn; int *cfgd_eqex;
	bool prune_ladders;
	bool boost_pass;
} uct_prior_t;

typedef struct prior_map {
	board_t *b;
	enum stone to_play;
	int parity;
	/* [board_size2(b)] array, move_stats are the prior
	 * values to be assigned to individual moves;
	 * move_stats.value is not updated. */
	move_stats_t *prior;
	/* [board_size2(b)] array, whether to compute
	 * prior for the given value. */
	bool *consider;
	/* [board_size2(b)] array from cfg_distances() */
	//int *distances;
} prior_map_t;

/* @value is the value, @playouts is its weight. */
static void add_prior_value(prior_map_t *map, coord_t c, floating_t value, int playouts);

void uct_prior(struct uct *u, tree_node_t *node, prior_map_t *map);

uct_prior_t *uct_prior_init(char *arg, board_t *b, struct uct *u);
void uct_prior_done(uct_prior_t *p);


static inline void
add_prior_value(prior_map_t *map, coord_t c, floating_t value, int playouts)
{
	floating_t v = map->parity > 0 ? value : 1 - value;
	/* We don't need atomicity: */
	move_stats_t s = move_stats(v, playouts);
	stats_merge(&map->prior[c], &s);
}

/* Display node's priors best moves */
void print_node_prior_best_moves(board_t *b, tree_node_t *parent);
void get_node_prior_best_moves(tree_node_t *parent, coord_t *best_c, float *best_r, int nbest);

#endif
