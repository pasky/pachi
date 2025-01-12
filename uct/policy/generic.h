#ifndef PACHI_UCT_POLICY_GENERIC_H
#define PACHI_UCT_POLICY_GENERIC_H

/* Some default policy routines and templates. */

#include "board.h"
#include "stone.h"
#include "uct/internal.h"

struct board;
struct tree_node;

tree_node_t *uctp_generic_choose(uct_policy_t *p, tree_node_t *node, board_t *b, enum stone color, coord_t exclude);
tree_node_t *uctp_generic_winner(uct_policy_t *p, tree_t *tree, tree_node_t *node);


/* Some generic stitching for tree descent. */

#if 0
#define uctd_debug(fmt...) fprintf(stderr, fmt);
#else
#define uctd_debug(fmt...)
#endif

/* Compute urgency for each child of @node.
 * Following urgency code is called for each child with @ni as 
 * current child node and stores its result in @urgency.  */
#define uctd_try_node_children(tree, node, allow_pass, parity, tenuki_d, ni, urgency)				\
	/* Information abound best children. */									\
	/* XXX: We assume board <=25x25. */									\
	tree_node_t *dbest[BOARD_MAX_MOVES + 1] = { node->children };						\
	int dbests = 1;												\
	floating_t best_urgency = -9999;									\
														\
	/* Descent children iterator. */									\
	for (tree_node_t *dci = node->children; dci; dci = dci->sibling) {					\
		floating_t urgency;										\
		/* Do not consider passing early. */								\
		if (unlikely((!allow_pass && is_pass(node_coord(dci))) || (dci->hints & TREE_HINT_INVALID)))	\
			continue;										\
		/* Set up descent-further iterator. This is the public-accessible one. */			\
		tree_node_t *ni = dci;										\

		/* ...your urgency computation code goes here... */


/* Keep track of children with highest urgency. */
#define uctd_set_best_child(ni, urgency)									\
		uctd_debug("(%s) %f\n", coord2sstr(node_coord(ni), tree->board), urgency);			\
		if (urgency - best_urgency > __FLT_EPSILON__) { /* urgency > best_urgency */			\
			uctd_debug("new best\n");								\
			best_urgency = urgency; dbests = 0;							\
		}												\
		if (urgency - best_urgency > -__FLT_EPSILON__) { /* urgency >= best_urgency */			\
			uctd_debug("another best\n");								\
			/* We want to always choose something else than a pass					\
			 * in case of a tie. pass causes degenerative behaviour. */				\
			if (dbests == 1 && is_pass(node_coord(dbest[0]))) {					\
				dbests--;									\
			}											\
			dbest[dbests++] = ni;									\
		}												\
	}

/* Select best child for descent */
#define uctd_get_best_child(node)	(dbest[fast_random(dbests)])


#endif
