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
uct_prior_one(struct uct *u, struct tree_node *node, struct prior_map *map, coord_t c)
{
	/* Initialization of UCT values based on prior knowledge */

	/* Q_{even} */
	/* This may be dubious for normal UCB1 but is essential for
	 * reading stability of RAVE, it appears. */
	if (u->even_eqex) {
		map->prior[c].playouts += u->even_eqex;
		map->prior[c].wins += u->even_eqex / 2;
	}

	/* Discourage playing into our own eyes. However, we cannot
	 * completely prohibit it:
	 * #######
	 * ...XX.#
	 * XOOOXX#
	 * X.OOOO#
	 * .XXXX.# */
	if (board_is_one_point_eye(map->b, &c, map->to_play)) {
		map->prior[c].playouts += u->eqex;
		map->prior[c].wins += map->parity > 0 ? 0 : u->eqex;
	}

	/* Q_{b19} */
	/* Specific hints for 19x19 board - priors for certain edge distances. */
	if (u->b19_eqex) {
		int d = coord_edge_distance(c, map->b);
		if (d == 1 || d == 3) {
			/* The bonus applies only with no stones in immediate
			 * vincinity. */
			if (!board_stone_radar(map->b, c, 2)) {
				/* First line: -eqex */
				/* Third line: +eqex */
				int v = d == 1 ? -1 : 1;
				map->prior[c].playouts += u->b19_eqex;
				map->prior[c].wins += map->parity * v > 0 ? u->b19_eqex : 0;
			}
		}
	}

	/* Q_{grandparent} */
	if (u->gp_eqex && node->parent && node->parent->parent && node->parent->parent->parent) {
		struct tree_node *gpp = node->parent->parent->parent;
		for (struct tree_node *ni = gpp->children; ni; ni = ni->sibling) {
			/* Be careful not to emphasize too random results. */
			if (ni->coord == node->coord && ni->u.playouts > u->gp_eqex) {
				map->prior[c].playouts += u->gp_eqex;
				map->prior[c].wins += u->gp_eqex * ni->u.wins / ni->u.playouts;
			}
		}
	}

	/* Q_{playout-policy} */
	if (u->policy_eqex) {
		int assess = 0;
		if (u->playout->assess) {
			struct move m = { c, map->to_play };
			assess = u->playout->assess(u->playout, map->b, &m, u->policy_eqex);
		}
		if (assess) {
			map->prior[c].playouts += abs(assess);
			/* Good moves for enemy are losses for us.
			 * We will properly maximize this in the UCB1
			 * decision. */
			assess *= map->parity;
			if (assess > 0) map->prior[c].wins += assess;
		}
	}
}

void
uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	foreach_point(map->b) {
		if (board_at(map->b, c) != S_NONE)
			continue;
		uct_prior_one(u, node, map, c);
	} foreach_point_end;
}
