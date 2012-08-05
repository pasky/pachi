#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "tactics/util.h"
#include "random.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/policy/generic.h"

struct tree_node *
uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color, coord_t exclude)
{
	struct tree_node *nbest = node->children;
	if (!nbest) return NULL;
	struct tree_node *nbest2 = nbest->sibling;

	/* This function is called while the tree is updated by other threads.
	 * We rely on node->children being set only after the node has been fully expanded. */
	for (struct tree_node *ni = nbest2; ni; ni = ni->sibling) {
		// we compare playouts and choose the best-explored
		// child; comparing values is more brittle
		if (node_coord(ni) == exclude || ni->hints & TREE_HINT_INVALID)
			continue;
		if (ni->u.playouts > nbest->u.playouts) {
			nbest2 = nbest;
			nbest = ni;
		} else if (ni->u.playouts > nbest2->u.playouts) {
			nbest2 = ni;
		}
	}
	/* Play pass only if we can afford scoring. Call expensive uct_pass_is_safe() only if
	 * pass is indeed the best move. */
	if (is_pass(node_coord(nbest)) && !uct_pass_is_safe(p->uct, b, color, p->uct->pass_all_alive))
		return nbest2;
	return nbest;
}

/* Return the node with best value instead of best explored. We must use the heuristic
 * value (using prior and possibly rave), because the raw value is meaningless for
 * nodes evaluated rarely.
 * This function is called while the tree is updated by other threads */
void
uctp_generic_winner(struct uct_policy *p, struct tree *tree, struct uct_descent *descent)
{
	if (!p->evaluate)
		return;
	bool allow_pass = false; /* At worst forces some extra playouts at the end */
	int parity = tree_node_parity(tree, descent->node);

	uctd_try_node_children(tree, descent, allow_pass, parity, p->uct->tenuki_d, di, urgency) {
		urgency = p->evaluate(p, tree, &di, parity);
	} uctd_set_best_child(di, urgency);

	uctd_get_best_child(descent);
}
