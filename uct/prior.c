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
		add_prior_value(map, c, u->even_eqex / 2, u->even_eqex);
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
		add_prior_value(map, c, -u->eqex, u->eqex);
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
		/* First line: -eqex */
		/* Third line: +eqex */
		int v = d == 1 ? -1 : 1;
		add_prior_value(map, c, v * u->b19_eqex, u->b19_eqex);
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
	foreach_point_and_pass(map->b) {
		if (!map->consider[c])
			continue;
		int assess = 0;
		if (u->playout->assess) {
			struct move m = { c, map->to_play };
			assess = u->playout->assess(u->playout, map->b, &m, u->policy_eqex);
		}
		if (!assess)
			continue;
		add_prior_value(map, c, assess, abs(assess));
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
}
