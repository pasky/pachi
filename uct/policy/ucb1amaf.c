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

/* This implements the UCB1 policy with an extra AMAF heuristics. */

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
	float explore_p_rave;
	int equiv_rave;
};


struct tree_node *ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

struct tree_node *ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);

void ucb1_prior(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int parity);


struct tree_node *
ucb1rave_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	struct ucb1_policy *b = p->data;
	float xpl = log(node->u.playouts) * b->explore_p;
	float xpl_rave = log(node->amaf.playouts) * b->explore_p_rave;
	float beta = sqrt((float)b->equiv_rave / (3 * node->u.playouts + b->equiv_rave));

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
		ni->amaf.value = (float)ni->amaf.wins / ni->amaf.playouts;
		float uctp = (parity > 0 ? ni->u.value : 1 - ni->u.value) + sqrt(xpl / ni->u.playouts);
		float ravep = (parity > 0 ? ni->amaf.value : 1 - ni->amaf.value) + sqrt(xpl_rave / ni->amaf.playouts);
		float urgency = ni->u.playouts ? beta * ravep + (1 - beta) * uctp : b->fpu;
		//fprintf(stderr, "u %f (%d/%d) r %f (%f %d/%d) b %f -> %f\n", uctp, ni->u.wins, ni->u.playouts, ravep, xpl_rave, ni->amaf.wins, ni->amaf.playouts, beta, urgency);
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

static void
update_node(struct uct_policy *p, struct tree_node *node, int result)
{
	node->u.playouts++;
	node->u.wins += result;
	tree_update_node_value(node, p->descend != ucb1rave_descend);
}
static void
update_node_amaf(struct uct_policy *p, struct tree_node *node, int result)
{
	node->amaf.playouts++;
	node->amaf.wins += result;
	tree_update_node_value(node, p->descend != ucb1rave_descend);
}

void
ucb1amaf_update(struct uct_policy *p, struct tree_node *node, enum stone color, struct playout_amafmap *map, int result)
{
	color = stone_other(color); // We will look in CHILDREN of the node!
	for (; node; node = node->parent, color = stone_other(color)) {
		/* Account for root node. */
		/* But we do the update everytime, since it simply seems
		 * to make more sense to give the main branch more weight
		 * than other orders of play. */
		update_node(p, node, result);
		if (is_pass(node->coord) || !node->parent)
			update_node_amaf(p, node, result);
		for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
			//struct board b; b.size = 9;
			//fprintf(stderr, "?%s [%d %d]\n", coord2sstr(ni->coord, &b), map->map[ni->coord], color);
			if (is_pass(ni->coord) || map->map[ni->coord] != color)
				continue;
			update_node_amaf(p, ni, result);
		}
	}
}


struct uct_policy *
policy_ucb1amaf_init(struct uct *u, char *arg)
{
	struct uct_policy *p = calloc(1, sizeof(*p));
	struct ucb1_policy *b = calloc(1, sizeof(*b));
	p->uct = u;
	p->data = b;
	p->descend = ucb1_descend;
	p->choose = ucb1_choose;
	p->update = ucb1amaf_update;
	p->wants_amaf = true;

	b->explore_p = 0.2;
	b->explore_p_rave = 0.2;
	b->equiv_rave = 3000;
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

			if (!strcasecmp(optname, "explore_p")) {
				b->explore_p = atof(optval);
			} else if (!strcasecmp(optname, "prior")) {
				b->eqex = optval ? atoi(optval) : 50;
				if (b->eqex)
					p->prior = ucb1_prior;
			} else if (!strcasecmp(optname, "fpu") && optval) {
				b->fpu = atof(optval);
			} else if (!strcasecmp(optname, "rave")) {
				p->descend = ucb1rave_descend;
			} else if (!strcasecmp(optname, "explore_p_rave")) {
				b->explore_p_rave = atof(optval);
			} else if (!strcasecmp(optname, "equiv_rave")) {
				b->equiv_rave = atof(optval);
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
