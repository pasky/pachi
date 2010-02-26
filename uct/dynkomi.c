#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "tactics.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/tree.h"


static void
uct_dynkomi_generic_done(struct uct_dynkomi *d)
{
	if (d->data) free(d->data);
	free(d);
}


/* NONE dynkomi strategy - never fiddle with komi values. */

struct uct_dynkomi *
uct_dynkomi_init_none(struct uct *u, char *arg, struct board *b)
{
	struct uct_dynkomi *d = calloc(1, sizeof(*d));
	d->uct = u;
	d->permove = NULL;
	d->persim = NULL;
	d->done = uct_dynkomi_generic_done;
	d->data = NULL;

	if (arg) {
		fprintf(stderr, "uct: Dynkomi method none accepts no arguments\n");
		exit(1);
	}

	return d;
}


/* LINEAR dynkomi strategy - Linearly Decreasing Handicap Compensation. */
/* At move 0, we impose extra komi of handicap_count*handicap_value, then
 * we linearly decrease this extra komi throughout the game down to 0
 * at @moves moves. */

struct dynkomi_linear {
	int handicap_value;
	int moves;
};

float
uct_dynkomi_linear_permove(struct uct_dynkomi *d, struct board *b, struct tree *tree)
{
	struct dynkomi_linear *l = d->data;
	if (b->moves >= l->moves)
		return 0;

	float base_komi = board_effective_handicap(b, l->handicap_value);
	float extra_komi = base_komi * (l->moves - b->moves) / l->moves;
	return extra_komi;
}

float
uct_dynkomi_linear_persim(struct uct_dynkomi *d, struct board *b, struct tree *tree, struct tree_node *node)
{
	/* We don't reuse computed value from tree->extra_komi,
	 * since we want to use value correct for this node depth.
	 * This also means the values will stay correct after
	 * node promotion. */
	return uct_dynkomi_linear_permove(d, b, tree);
}

struct uct_dynkomi *
uct_dynkomi_init_linear(struct uct *u, char *arg, struct board *b)
{
	struct uct_dynkomi *d = calloc(1, sizeof(*d));
	d->uct = u;
	d->permove = uct_dynkomi_linear_permove;
	d->persim = uct_dynkomi_linear_persim;
	d->done = uct_dynkomi_generic_done;

	struct dynkomi_linear *l = calloc(1, sizeof(*l));
	d->data = l;

	if (board_size(b) - 2 >= 19)
		l->moves = 200;
	l->handicap_value = 7;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "moves") && optval) {
				/* Dynamic komi in handicap game; linearly
				 * decreases to basic settings until move
				 * #optval. */
				l->moves = atoi(optval);
			} else if (!strcasecmp(optname, "handicap_value") && optval) {
				/* Point value of single handicap stone,
				 * for dynkomi computation. */
				l->handicap_value = atoi(optval);
			} else {
				fprintf(stderr, "uct: Invalid dynkomi argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	return d;
}
