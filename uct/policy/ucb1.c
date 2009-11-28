#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/policy/generic.h"

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
};


struct tree_node *
ucb1_descend(struct uct_policy *p, void **state, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	/* We want to count in the prior stats here after all. Otherwise,
	 * nodes with positive prior will get explored _LESS_ since the
	 * urgency will be always higher; even with normal FPU because
	 * of the explore coefficient. */

	struct ucb1_policy *b = p->data;
	float xpl = log(node->u.playouts + node->prior.playouts) * b->explore_p;

	uctd_try_node_children(node, allow_pass, ni, urgency) {
		int uct_playouts = ni->u.playouts + ni->prior.playouts;

		if (uct_playouts) {
			/* prior-normal ratio. */
			float alpha = ni->u.playouts / uct_playouts;
			urgency = alpha * tree_node_get_value(tree, parity, ni->u.value)
				+ (1 - alpha) * tree_node_get_value(tree, parity, ni->prior.value);
			urgency += sqrt(xpl / uct_playouts);
		} else {
			urgency = b->fpu;
		}
	} uctd_set_best_child(ni, urgency);
	return uctd_get_best_child();
}

void
ucb1_update(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone node_color, enum stone player_color, struct playout_amafmap *map, float result)
{
	/* It is enough to iterate by a single chain; we will
	 * update all the preceding positions properly since
	 * they had to all occur in all branches, only in
	 * different order. */
	for (; node; node = node->parent) {
		stats_add_result(&node->u, result, 1);
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
	p->choose = uctp_generic_choose;
	p->update = ucb1_update;

	b->explore_p = 0.2;
	b->fpu = 1.1; //INFINITY;

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
			} else if (!strcasecmp(optname, "fpu") && optval) {
				b->fpu = atof(optval);
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n",
					optname);
				exit(1);
			}
		}
	}

	return p;
}
