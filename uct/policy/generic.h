#ifndef ZZGO_UCT_POLICY_GENERIC_H
#define ZZGO_UCT_POLICY_GENERIC_H

/* Some default policy routines and templates. */

#include "stone.h"
#include "uct/internal.h"

struct board;
struct tree_node;

struct tree_node *uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color, coord_t exclude);
void uctp_generic_winner(struct uct_policy *p, struct tree *tree, struct uct_descent *descent);


/* Some generic stitching for tree descent. */

#define uctd_try_node_children(tree, descent, allow_pass, di, urgency) \
	/* XXX: Stack overflow danger on big boards? */ \
	struct uct_descent dbest[512] = { { .node = descent->node->children } }; int dbests = 1; \
	float best_urgency = -9999; \
	struct uct_descent di = { .node = descent->node->children }; \
	\
	for (; di.node; di.node = di.node->sibling) { \
		float urgency; \
		/* Do not consider passing early. */ \
		if (unlikely((!allow_pass && is_pass(di.node->coord)) || (di.node->hints & TREE_HINT_INVALID))) \
			continue;

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
			dbest[dbests++] = di; \
		} \
	}

#define uctd_get_best_child(descent) *(descent) = dbest[fast_random(dbests)];


#endif
