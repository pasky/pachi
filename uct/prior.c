#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"

/* Applying heuristic values to the tree nodes, skewing the reading in
 * most interesting directions. */


void
uct_prior(struct uct *u, struct tree *tree, struct tree_node *node,
          struct board *b, enum stone color, int parity)
{
	/* Initialization of UCT values based on prior knowledge */

	/* Q_{even} */
	/* This may be dubious for normal UCB1 but is essential for
	 * reading stability of RAVE, it appears. */
	if (u->even_eqex) {
		node->prior.playouts += u->even_eqex;
		node->prior.wins += u->even_eqex / 2;
	}

	/* Discourage playing into our own eyes. However, we cannot
	 * completely prohibit it:
	 * ######
	 * ...XX.
	 * XOOOXX
	 * X.OOOO
	 * .XXXX. */
	if (board_is_one_point_eye(b, &node->coord, color)) {
		node->prior.playouts += u->eqex;
		node->prior.wins += tree_parity(tree, parity) > 0 ? 0 : u->eqex;
	}

	/* Q_{grandparent} */
	if (u->gp_eqex && node->parent && node->parent->parent && node->parent->parent->parent) {
		struct tree_node *gpp = node->parent->parent->parent;
		for (struct tree_node *ni = gpp->children; ni; ni = ni->sibling) {
			/* Be careful not to emphasize too random results. */
			if (ni->coord == node->coord && ni->u.playouts > u->gp_eqex) {
				node->prior.playouts += u->gp_eqex;
				node->prior.wins += u->gp_eqex * ni->u.wins / ni->u.playouts;
				node->hints |= 1;
			}
		}
	}

	/* Q_{playout-policy} */
	if (u->policy_eqex) {
		int assess = 0;
		struct playout_policy *playout = u->playout;
		if (playout->assess) {
			struct move m = { node->coord, color };
			assess = playout->assess(playout, b, &m, u->policy_eqex);
		}
		if (assess) {
			node->prior.playouts += abs(assess);
			/* Good moves for enemy are losses for us.
			 * We will properly maximize this in the UCB1
			 * decision. */
			assess *= tree_parity(tree, parity);
			if (assess > 0) node->prior.wins += assess;
			node->hints |= 2;
		}
	}

	if (node->prior.playouts) {
		node->prior.value = (float) node->prior.wins / node->prior.playouts;
		tree_update_node_value(node, u->amaf_prior);
	}

	//fprintf(stderr, "%s,%s prior: %d/%d = %f (%f)\n", coord2sstr(node->parent->coord, b), coord2sstr(node->coord, b), node->prior.wins, node->prior.playouts, node->prior.value, assess);
}
