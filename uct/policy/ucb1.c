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
	int eqex, even_eqex, gp_eqex, policy_eqex;
	int urg_randoma, urg_randomm;
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
	/* We want to count in the prior stats here after all. Otherwise,
	 * nodes with positive prior will get explored _LESS_ since the
	 * urgency will be always higher; even with normal FPU because
	 * of the explore coefficient. */

	struct ucb1_policy *b = p->data;
	float xpl = log(node->u.playouts + node->prior.playouts) * b->explore_p;

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
		int uct_playouts = ni->u.playouts + ni->prior.playouts;
		ni->prior.value = (float)ni->prior.wins / ni->prior.playouts;
		float urgency = uct_playouts ? (parity > 0 ? ni->u.value : 1 - ni->u.value) + sqrt(xpl / uct_playouts) : b->fpu;

#if 0
		{
			struct board b2; b2.size = 9+2;
			fprintf(stderr, "[%s -> %s] UCB1 urgency %f (%f + %f : %f)\n", coord2sstr(node->coord, &b2), coord2sstr(ni->coord, &b2), urgency, ni->u.value, sqrt(xpl / ni->u.playouts), b->fpu);
		}
#endif
		if (b->urg_randoma)
			urgency += (float)(fast_random(b->urg_randoma) - b->urg_randoma / 2) / 1000;
		if (b->urg_randomm)
			urgency *= (float)(fast_random(b->urg_randomm) + 5) / b->urg_randomm;
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

	/* Q_{even} */
	/* This may be dubious for normal UCB1 but is essential for
	 * reading stability of RAVE, it appears. */
	if (pp->even_eqex) {
		node->prior.playouts += pp->even_eqex;
		node->prior.wins += pp->even_eqex / 2;
	}

	/* Q_{grandparent} */
	if (pp->gp_eqex && node->parent && node->parent->parent && node->parent->parent->parent) {
		struct tree_node *gpp = node->parent->parent->parent;
		for (struct tree_node *ni = gpp->children; ni; ni = ni->sibling) {
			/* Be careful not to emphasize too random results. */
			if (ni->coord == node->coord && ni->u.playouts > pp->gp_eqex) {
				node->prior.playouts += pp->gp_eqex;
				node->prior.wins += pp->gp_eqex * ni->u.wins / ni->u.playouts;
				node->hints |= 1;
			}
		}
	}

	/* Q_{playout-policy} */
	if (pp->policy_eqex) {
		float assess = NAN;
		struct playout_policy *playout = p->uct->playout;
		if (playout->assess) {
			struct move m = { node->coord, color };
			assess = playout->assess(playout, b, &m);
		}
		if (!isnan(assess)) {
			if (parity < 0) {
				/* Good moves for enemy are losses for us.
				 * We will properly maximize this in the UCB1
				 * decision. */
				assess = 1 - assess;
			}
			node->prior.playouts += pp->policy_eqex;
			node->prior.wins += pp->policy_eqex * assess;
			node->hints |= 2;
		}
	}

	if (node->prior.playouts) {
		node->prior.value = (float) node->prior.wins / node->prior.playouts;
		tree_update_node_value(node);
	}

	//fprintf(stderr, "%s,%s prior: %d/%d = %f (%f)\n", coord2sstr(node->parent->coord, b), coord2sstr(node->coord, b), node->prior.wins, node->prior.playouts, node->prior.value, assess);
}

void
ucb1_update(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone node_color, enum stone player_color, struct playout_amafmap *map, int result)
{
	/* It is enough to iterate by a single chain; we will
	 * update all the preceding positions properly since
	 * they had to all occur in all branches, only in
	 * different order. */
	for (; node; node = node->parent) {
		node->u.playouts++;
		node->u.wins += result;
		tree_update_node_value(node);
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
	b->even_eqex = 0;
	b->gp_eqex = b->policy_eqex = -1;
	b->eqex = 50;

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
				if (optval)
					b->eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prior_even") && optval) {
				b->even_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prior_gp") && optval) {
				b->gp_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prior_policy") && optval) {
				b->policy_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "fpu") && optval) {
				b->fpu = atof(optval);
			} else if (!strcasecmp(optname, "urg_randoma") && optval) {
				b->urg_randoma = atoi(optval);
			} else if (!strcasecmp(optname, "urg_randomm") && optval) {
				b->urg_randomm = atoi(optval);
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	if (b->eqex) p->prior = ucb1_prior;
	if (b->even_eqex < 0) b->even_eqex = b->eqex;
	if (b->gp_eqex < 0) b->gp_eqex = b->eqex;
	if (b->policy_eqex < 0) b->policy_eqex = b->eqex;

	return p;
}
