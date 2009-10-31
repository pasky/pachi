#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/policy/generic.h"

struct tree_node *
uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color)
{
	struct tree_node *nbest = NULL;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		// we compare playouts and choose the best-explored
		// child; comparing values is more brittle
		if (!nbest || ni->u.playouts > nbest->u.playouts) {
			/* Play pass only if we can afford scoring */
			if (is_pass(ni->coord) && !uct_pass_is_safe(p->uct, b, color))
				continue;
			nbest = ni;
		}
	return nbest;
}
