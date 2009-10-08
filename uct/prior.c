#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"

/* Applying heuristic values to the tree nodes, skewing the reading in
 * most interesting directions. */


void
uct_prior_even(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{even} */
	/* This may be dubious for normal UCB1 but is essential for
	 * reading stability of RAVE, it appears. */
	foreach_point_and_pass(map->b) {
		if (!map->consider[c])
			continue;
		add_prior_value(map, c, 0.5, u->even_eqex);
	} foreach_point_end;
}

void
uct_prior_eye(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Discourage playing into our own eyes. However, we cannot
	 * completely prohibit it:
	 * #######
	 * ...XX.#
	 * XOOOXX#
	 * X.OOOO#
	 * .XXXX.# */
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		if (!board_is_one_point_eye(map->b, &c, map->to_play))
			continue;
		add_prior_value(map, c, 0, u->eqex);
	} foreach_point_end;
}

void
uct_prior_b19(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{b19} */
	/* Specific hints for 19x19 board - priors for certain edge distances. */
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		int d = coord_edge_distance(c, map->b);
		if (d != 1 && d != 3)
			continue;
		/* The bonus applies only with no stones in immediate
		 * vincinity. */
		if (board_stone_radar(map->b, c, 2))
			continue;
		/* First line: 0 */
		/* Third line: 1 */
		add_prior_value(map, c, d == 3, u->b19_eqex);
	} foreach_point_end;
}

void
uct_prior_grandparent(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{grandparent} */
	foreach_point_and_pass(map->b) {
		if (!map->consider[c])
			continue;
		if (!node->parent || !node->parent->parent)
			continue;
		struct tree_node *gpp = node->parent->parent;
		for (struct tree_node *ni = gpp->children; ni; ni = ni->sibling) {
			/* Be careful not to emphasize too random results. */
			if (ni->coord == node->coord && ni->u.playouts > u->gp_eqex) {
				/* We purposefuly ignore the parity. */
				map->prior[c].playouts += u->gp_eqex;
				map->prior[c].wins += u->gp_eqex * ni->u.wins / ni->u.playouts;
			}
		}
	} foreach_point_end;
}

void
uct_prior_playout(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{playout-policy} */
	if (u->playout->assess)
		u->playout->assess(u->playout, map, u->policy_eqex);
}

void
uct_prior_cfgd(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{common_fate_graph_distance} */
	/* Give bonus to moves local to the last move, where "local" means
	 * local in terms of groups, not just manhattan distance. */
	if (is_pass(map->b->last_move.coord))
		return;

	int distances[board_size2(map->b)];
	cfg_distances(map->b, map->b->last_move.coord, distances, 3);
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		// fprintf(stderr, "distance %s-%s: %d\n", coord2sstr(map->b->last_move.coord, map->b), coord2sstr(c, map->b), distances[c]);
		if (distances[c] > 3)
			continue;
		assert(distances[c] != 0);
		int bonuses[] = { 0, u->cfgd_eqex, u->cfgd_eqex / 2, u->cfgd_eqex / 2 };
		int bonus = bonuses[distances[c]];
		add_prior_value(map, c, 1, bonus);
	} foreach_point_end;
}

void
uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	if (u->even_eqex)
		uct_prior_even(u, node, map);
	uct_prior_eye(u, node, map);
	if (u->b19_eqex)
		uct_prior_b19(u, node, map);
	if (u->gp_eqex)
		uct_prior_grandparent(u, node, map);
	if (u->policy_eqex)
		uct_prior_playout(u, node, map);
	if (u->cfgd_eqex)
		uct_prior_cfgd(u, node, map);
}
