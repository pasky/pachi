#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "tactics.h"
#include "random.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/policy/generic.h"

struct tree_node *
uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color)
{
	struct tree_node *nbest = NULL;
	/* This function is called while the tree is updated by other threads.
	 * We rely on node->children being set only after the node has been fully expanded. */
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		// we compare playouts and choose the best-explored
		// child; comparing values is more brittle
		if (!nbest || ni->u.playouts > nbest->u.playouts) {
			/* Play pass only if we can afford scoring */
			if (is_pass(ni->coord) && !uct_pass_is_safe(p->uct, b, color, p->uct->pass_all_alive))
				continue;
			nbest = ni;
		}
	return nbest;
}

/* Return the node with best value instead of best explored. We must use the heuristic
 * value (using prior and possibly rave), because the raw value is meaningless for
 * nodes evaluated rarely.
 * This function is called while the tree is updated by other threads */
struct tree_node *
uctp_generic_winner(struct uct_policy *p, struct tree *tree, struct tree_node *node)
{
	if (!p->evaluate)
		return NULL;
	bool allow_pass = false; /* At worst forces some extra playouts at the end */
	void *state; /* TODO: remove this unused parameter. */
	int parity = ((node->depth ^ tree->root->depth) & 1) ? -1 : 1;

	uctd_try_node_children(node, allow_pass, ni, urgency) {
		urgency = p->evaluate(p, state, tree, ni, parity);
	} uctd_set_best_child(ni, urgency);

	return uctd_get_best_child();
}
