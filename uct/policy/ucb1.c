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

struct ucb1_policy {
	/* This is what the Modification of UCT with Patterns in Monte Carlo Go
	 * paper calls 'p'. Original UCB has this on 2, but this seems to
	 * produce way too wide searches; reduce this to get deeper and
	 * narrower readouts - try 0.2. */
	float explore_p;
};


struct tree_node *
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


struct tree_node *
ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	struct ucb1_policy *b = p->data;
	float xpl = log(node->playouts) * b->explore_p;

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
		float urgency = ni->value * parity + sqrt(xpl / ni->playouts);
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

void
ucb1_prior(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color)
{
	/* Initialization of UCT values based on prior knowledge */

	int eqex = 50; // majic

#if 0
	/* Q_{even} */
	/* This somehow does not work at all. */
	node->playouts += eqex;
	node->wins += eqex / 2;
#endif

	/* Q_{grandparent} */
	struct tree_node *gp = node->parent ? node->parent->parent : NULL;
	if (gp) {
		for (struct tree_node *ni = gp->children; ni; ni = ni->sibling) {
			if (ni->coord == node->coord) {
				/* Be careful not to emphasize too random results. */
				int playouts = ni->playouts, wins = ni->wins;
				if (playouts > eqex) {
					playouts = eqex;
					wins = eqex * wins / playouts;
				}
				node->playouts += playouts;
				node->wins += wins;
			}
		}
	}

	/* Q_{playout-policy} */
	float assess = NAN;
	struct playout_policy *playout = p->uct->playout;
	if (playout->assess) {
		struct move m = { node->coord, color };
		assess = playout->assess(playout, b, &m);
	}
	if (!isnan(assess)) {
		node->playouts += eqex;
		node->wins += eqex * assess;
	}

	if (node->playouts)
		node->value = node->wins / node->playouts;

	//fprintf(stderr, "prior: %d/%d\n", node->wins, node->playouts);
}

void
ucb1_update(struct uct_policy *p, struct tree_node *node, struct playout_amafmap *map, int result)
{
	/* It is enough to iterate by a single chain; we will
	 * update all the preceding positions properly since
	 * they had to all occur in all branches, only in
	 * different order. */
	for (; node; node = node->parent) {
		node->playouts++;
		node->wins += result;
		node->value = (float)node->wins / node->playouts;
	}
}


struct uct_policy *
policy_ucb1_init(struct uct *u, char *arg)
{
	struct uct_policy *p = calloc(1, sizeof(*p));
	struct ucb1_policy *b = calloc(1, sizeof(*b));
	p->uct = u;
	p->data = b;
	p->descend = ucb1_descend;
	p->choose = ucb1_choose;
	p->update = ucb1_update;

	b->explore_p = 0.2;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "explore_p")) {
				b->explore_p = atof(optval);
			} else if (!strcasecmp(optname, "prior")) {
				p->prior = ucb1_prior;
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
