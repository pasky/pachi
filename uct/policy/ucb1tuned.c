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

/* This implements the UCB1-TUNED policy. */

struct ucb1_policy {
	/* This is what the Modification of UCT with Patterns in Monte Carlo Go
	 * paper calls 'p'. Original UCB has this on 2, but this seems to
	 * produce way too wide searches; reduce this to get deeper and
	 * narrower readouts - try 0.2. */
	float explore_p;
};


struct tree_node *ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

struct tree_node *
ucb1tuned_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	struct boardpos *pos = node->pos;
	struct ucb1_policy *b = p->data;
	float xpl = log(pos->playouts) * b->explore_p;

	struct tree_node *nbest = pos->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = pos->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
		float xpl_loc = (ni->pos->value - ni->pos->value * ni->pos->value);
		if (parity < 0) xpl_loc = 1 - xpl_loc;
		xpl_loc += sqrt(xpl / ni->pos->playouts);
		if (xpl_loc > 1.0/4) xpl_loc = 1.0/4;
		float urgency = ni->pos->value * parity + sqrt(xpl * xpl_loc / ni->pos->playouts);
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

void ucb1_update(struct uct_policy *p, struct tree_node *node, struct playout_amafmap *map, int result);


struct uct_policy *
policy_ucb1tuned_init(struct uct *u, char *arg)
{
	struct uct_policy *p = calloc(1, sizeof(*p));
	struct ucb1_policy *b = calloc(1, sizeof(*b));
	p->uct = u;
	p->data = b;
	p->descend = ucb1tuned_descend;
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
			} else {
				fprintf(stderr, "ucb1tuned: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
