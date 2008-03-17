#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "uct/internal.h"
#include "uct/tree.h"

/* This implements the basic UCB1 policy. */


static struct tree_node *
ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color)
{
	struct tree_node *nbest = NULL;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		// we compare playouts and choose the best-explored
		// child; comparing values is more brittle
		if (!nbest || ni->playouts > nbest->playouts) {
			/* Play pass only if we can afford scoring */
			if (is_pass(ni->coord)) {
				float score = board_official_score(b);
				if (color == S_BLACK)
					score = -score;
				//fprintf(stderr, "%d score %f\n", color, score);
				if (score <= 0)
					continue;
			}
			nbest = ni;
		}
	return nbest;
}


static struct tree_node *
ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	float xpl = log(node->playouts) * tree->explore_p;

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
#ifdef UCB1_TUNED
		float xpl_loc = (ni->value - ni->value * ni->value);
		if (parity < 0) xpl_loc = 1 - xpl_loc;
		xpl_loc += sqrt(xpl / ni->playouts);
		if (xpl_loc > 1.0/4) xpl_loc = 1.0/4;
		float urgency = ni->value * parity + sqrt(xpl * xpl_loc / ni->playouts);
#else
		float urgency = ni->value * parity + sqrt(xpl / ni->playouts);
#endif
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}


struct uct_policy *
policy_ucb1_init(struct uct *u)
{
	struct uct_policy *p = calloc(1, sizeof(*p));
	p->uct = u;
	p->descend = ucb1_descend;
	p->choose = ucb1_choose;
	return p;
}
