#ifndef ZZGO_UCT_POLICY_GENERIC_H
#define ZZGO_UCT_POLICY_GENERIC_H

/* Some default policy routines and templates. */

#include "board.h"
#include "stone.h"
#include "uct/internal.h"

struct board;
struct tree_node;

struct tree_node *uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color, coord_t exclude);
void uctp_generic_winner(struct uct_policy *p, struct tree *tree, struct uct_descent *descent);


/* Some generic stitching for tree descent. */

#define uctd_try_node_children(tree, descent, allow_pass, parity, tenuki_d, di, urgency) \
	/* Information abound best children. */ \
	/* XXX: We assume board <=25x25. */ \
	struct uct_descent dbest[BOARD_MAX_MOVES + 1] = { { .node = descent->node->children, .lnode = NULL } }; int dbests = 1; \
	float best_urgency = -9999; \
	/* Descent children iterator. */ \
	struct uct_descent dci = { .node = descent->node->children, .lnode = descent->lnode ? descent->lnode->children : NULL }; \
	\
	for (; dci.node; dci.node = dci.node->sibling) { \
		float urgency; \
		/* Do not consider passing early. */ \
		if (unlikely((!allow_pass && is_pass(dci.node->coord)) || (dci.node->hints & TREE_HINT_INVALID))) \
			continue; \
		/* Position dci.lnode to point at or right after the local
		 * node corresponding to dci.node. */ \
		while (dci.lnode && dci.lnode->coord < dci.node->coord) \
			dci.lnode = dci.lnode->sibling; \
		/* Set up descent-further iterator. This is the public-accessible
		 * one, and usually is similar to dci. However, in case of local
		 * trees, we may keep next-candidate pointer in dci while storing
		 * actual-specimen in di. */ \
		struct uct_descent di = dci; \
		if (dci.lnode) { \
			/* Set lnode to local tree node corresponding
			 * to node (dci.lnode, pass-lnode or NULL). */ \
			di.lnode = tree_lnode_for_node(tree, dci.node, dci.lnode, tenuki_d); \
		}

		/* ...your urgency computation code goes here... */

#define uctd_set_best_child(di, urgency) \
		if (urgency - best_urgency > __FLT_EPSILON__) { /* urgency > best_urgency */ \
			best_urgency = urgency; dbests = 0; \
		} \
		if (urgency - best_urgency > -__FLT_EPSILON__) { /* urgency >= best_urgency */ \
			/* We want to always choose something else than a pass \
			 * in case of a tie. pass causes degenerative behaviour. */ \
			if (dbests == 1 && is_pass(dbest[0].node->coord)) { \
				dbests--; \
			} \
			struct uct_descent db = di; \
			/* Make sure lnode information is meaningful. */ \
			if (db.lnode && is_pass(db.lnode->coord)) \
				db.lnode = NULL; \
			dbest[dbests++] = db; \
		} \
	}

#define uctd_get_best_child(descent) *(descent) = dbest[fast_random(dbests)];


#endif
