#ifndef PACHI_UCT_POLICY_GENERIC_H
#define PACHI_UCT_POLICY_GENERIC_H

/* Some default policy routines and templates. */

#include "board.h"
#include "stone.h"
#include "uct/internal.h"

struct board;
struct tree_node;

tree_node_t *uctp_generic_choose(uct_policy_t *p, tree_node_t *node, board_t *b, enum stone color, coord_t exclude);
void uctp_generic_winner(uct_policy_t *p, tree_t *tree, uct_descent_t *descent);


/* Some generic stitching for tree descent. */

#if 0
#define uctd_debug(fmt...) fprintf(stderr, fmt);
#else
#define uctd_debug(fmt...)
#endif

#define uctd_try_node_children(tree, descent, allow_pass, parity, tenuki_d, di, urgency) \
	/* Information abound best children. */ \
	/* XXX: We assume board <=25x25. */ \
	uct_descent_t dbest[BOARD_MAX_MOVES + 1] = { uct_descent(descent->node->children) }; int dbests = 1; \
	floating_t best_urgency = -9999; \
	/* Descent children iterator. */ \
	uct_descent_t dci = uct_descent(descent->node->children); \
	\
	for (; dci.node; dci.node = dci.node->sibling) { \
		floating_t urgency; \
		/* Do not consider passing early. */ \
		if (unlikely((!allow_pass && is_pass(node_coord(dci.node))) || (dci.node->hints & TREE_HINT_INVALID))) \
			continue; \
		/* Set up descent-further iterator. This is the public-accessible
		 * one, and usually is similar to dci. However, in case of local
		 * trees, we may keep next-candidate pointer in dci while storing
		 * actual-specimen in di. */ \
		uct_descent_t di = dci; \

		/* ...your urgency computation code goes here... */

#define uctd_set_best_child(di, urgency) \
		uctd_debug("(%s) %f\n", coord2sstr(node_coord(di.node), tree->board), urgency); \
		if (urgency - best_urgency > __FLT_EPSILON__) { /* urgency > best_urgency */ \
			uctd_debug("new best\n"); \
			best_urgency = urgency; dbests = 0; \
		} \
		if (urgency - best_urgency > -__FLT_EPSILON__) { /* urgency >= best_urgency */ \
			uctd_debug("another best\n"); \
			/* We want to always choose something else than a pass \
			 * in case of a tie. pass causes degenerative behaviour. */ \
			if (dbests == 1 && is_pass(node_coord(dbest[0].node))) { \
				dbests--; \
			} \
			uct_descent_t db = di; \
			dbest[dbests++] = db; \
		} \
	}

#define uctd_get_best_child(descent) *(descent) = dbest[fast_random(dbests)];


#endif
