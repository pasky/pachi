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
	/* First Play Urgency - if set to less than infinity (the MoGo paper
	 * above reports 1.0 as the best), new branches are explored only
	 * if none of the existing ones has higher urgency than fpu. */
	float fpu;
	/* Equivalent experience for prior knowledge. MoGo paper recommends
	 * 50 playouts per source. */
	int eqex;
};


struct tree_node *
ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color)
{
	struct tree_node *nbest = NULL;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		// we compare playouts and choose the best-explored
		// child; comparing values is more brittle
		if (!nbest || ni->u.playouts > nbest->u.playouts) {
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
	float xpl = log(node->u.playouts) * b->explore_p;

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
		float urgency = ni->u.playouts ? (parity > 0 ? ni->u.value : 1 - ni->u.value) + sqrt(xpl / ni->u.playouts) : b->fpu;
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

void
ucb1_prior(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int parity)
{
	/* Initialization of UCT values based on prior knowledge */
	struct ucb1_policy *pp = p->data;

#if 0
	/* Q_{even} */
	/* This somehow does not work at all. */
	node->prior.playouts += p->eqex;
	node->prior.wins += p->eqex / 2;
#endif

	/* Q_{grandparent} */
	if (node->parent && node->parent->parent && node->parent->parent->parent) {
		struct tree_node *gpp = node->parent->parent->parent;
		for (struct tree_node *ni = gpp->children; ni; ni = ni->sibling) {
			/* Be careful not to emphasize too random results. */
			if (ni->coord == node->coord && ni->u.playouts > pp->eqex) {
				node->prior.playouts += pp->eqex;
				node->prior.wins += pp->eqex * ni->u.wins / ni->u.playouts;
				node->hints |= 1;
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
		if (parity < 0)
			assess = 1 - assess;
		node->prior.playouts += pp->eqex;
		node->prior.wins += pp->eqex * assess;
		node->hints |= 2;
	}

	if (node->prior.playouts) {
		node->prior.value = (float) node->prior.wins / node->prior.playouts;
		tree_update_node_value(node, true);
	}

	//fprintf(stderr, "%s,%s prior: %d/%d = %f (%f)\n", coord2sstr(node->parent->coord, b), coord2sstr(node->coord, b), node->prior.wins, node->prior.playouts, node->prior.value, assess);
}

void
ucb1_update(struct uct_policy *p, struct tree_node *node, enum stone color, struct playout_amafmap *map, int result)
{
	/* It is enough to iterate by a single chain; we will
	 * update all the preceding positions properly since
	 * they had to all occur in all branches, only in
	 * different order. */
	for (; node; node = node->parent) {
		node->u.playouts++;
		node->u.wins += result;
		tree_update_node_value(node, true);
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
	b->fpu = INFINITY;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "explore_p") && optval) {
				b->explore_p = atof(optval);
			} else if (!strcasecmp(optname, "prior")) {
				b->eqex = optval ? atoi(optval) : 50;
				if (b->eqex)
					p->prior = ucb1_prior;
			} else if (!strcasecmp(optname, "fpu") && optval) {
				b->fpu = atof(optval);
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
