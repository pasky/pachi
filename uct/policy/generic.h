#ifndef ZZGO_UCT_POLICY_GENERIC_H
#define ZZGO_UCT_POLICY_GENERIC_H

/* Some default policy routines and templates. */

#include "stone.h"
#include "uct/internal.h"

struct board;
struct tree_node;

struct tree_node *uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color, coord_t exclude);
struct tree_node *uctp_generic_winner(struct uct_policy *p, struct tree *tree, struct tree_node *node);


/* Some generic stitching for tree descent. */

#define uctd_try_node_children(node, allow_pass, ni, urgency) \
	/* XXX: Stack overflow danger on big boards? */ \
	struct tree_node *nbest[512] = { node->children }; int nbests = 1; \
	float best_urgency = -9999; \
	\
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) { \
		float urgency; \
		/* Do not consider passing early. */ \
		if (unlikely((!allow_pass && is_pass(ni->coord)) || (ni->hints & TREE_HINT_INVALID))) \
			continue;

		/* ...your urgency computation code goes here... */

#define uctd_set_best_child(ni, urgency) \
		if (urgency - best_urgency > __FLT_EPSILON__) { /* urgency > best_urgency */ \
			best_urgency = urgency; nbests = 0; \
		} \
		if (urgency - best_urgency > -__FLT_EPSILON__) { /* urgency >= best_urgency */ \
			/* We want to always choose something else than a pass \
			 * in case of a tie. pass causes degenerative behaviour. */ \
			if (nbests == 1 && is_pass(nbest[0]->coord)) { \
				nbests--; \
			} \
			nbest[nbests++] = ni; \
		} \
	}

#define uctd_get_best_child() nbest[fast_random(nbests)]


#endif
