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
};


struct tree_node *ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

struct tree_node *ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);

static void
update_node(struct uct_policy *p, struct tree_node *node, int result)
{
	struct boardpos *pos = node->pos;
	pos->playouts++;
	pos->wins += result;
	pos->value = (float)pos->wins / pos->playouts;
}

void
ucb1amaf_update(struct uct_policy *p, struct tree_node *node, struct playout_amafmap *map, int result)
{
	enum stone color = map->color;
	for (; node; node = node->parent, color = stone_other(color)) {
		/* Account for root node. */
		/* But we do the update everytime, since it simply seems
		 * to make more sense to give the main branch more weight
		 * than other orders of play. */
		update_node(p, node, result);
		for (struct tree_node *ni = node->pos->children; ni; ni = ni->sibling) {
			if (is_pass(ni->coord) || map->map[ni->coord] != color)
				continue;
			update_node(p, node, result);
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
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
