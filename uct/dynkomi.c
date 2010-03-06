#define DEBUG
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
	bool rootbased;
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
	struct dynkomi_linear *l = d->data;
	if (l->rootbased)
		return tree->extra_komi;
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
			} else if (!strcasecmp(optname, "rootbased")) {
				/* If set, the extra komi applied will be
				 * the same for all simulations within a move,
				 * instead of being same for all simulations
				 * within the tree node. */
				l->rootbased = !optval || atoi(optval);
			} else {
				fprintf(stderr, "uct: Invalid dynkomi argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	return d;
}


/* ADAPTIVE dynkomi strategy - Adaptive Situational Compensation */
/* We adapt the komi based on current situation:
 * (i) score-based: We maintain the average score outcome of our
 * games and adjust the komi by a fractional step towards the expected
 * score;
 * (ii) value-based: While winrate is above given threshold, adjust
 * the komi by a fixed step in the appropriate direction.
 * These adjustments can be
 * (a) Move-stepped, new extra komi value is always set only at the
 * beginning of the tree search for next move;
 * (b) Continuous, new extra komi value is periodically re-determined
 * and adjusted throughout a single tree search. [TODO] */

struct dynkomi_adaptive {
	/* Do not take measured average score into regard for
	 * first @lead_moves - the variance is just too much.
	 * (Instead, we consider the handicap-based komi provided
	 * by linear dynkomi.) */
	int lead_moves;
	/* Maximum komi to pretend the opponent to give. */
	float max_losing_komi;

	/* Value-based adaptation. */
	bool value_based;
	float zone_red, zone_green;
	int score_step;
	float komi_latch; // runtime, not configuration

	float (*adapter)(struct dynkomi_adaptive *a, struct board *b);
	float adapt_base; // [0,1)
	/* Sigmoid adaptation rate parameter; see below for details. */
	float adapt_phase; // [0,1]
	float adapt_rate; // [1,infty)
	/* Linear adaptation rate parameter. */
	int adapt_moves;
	float adapt_dir; // [-1,1]
};
#define TRUSTWORTHY_KOMI_PLAYOUTS 200

float
adapter_sigmoid(struct dynkomi_adaptive *a, struct board *b)
{
	/* Figure out how much to adjust the komi based on the game
	 * stage. The adaptation rate is ~0.9 at the beginning,
	 * at game stage a->adapt_phase crosses though 0.5 and
	 * approaches 0 at the game end; the slope is controlled
	 * by a->adapt_rate. */
	int total_moves = b->moves + board_estimated_moves_left(b);
	float game_portion = (float) b->moves / total_moves;
	float l = -game_portion + a->adapt_phase;
	return 1.0 / (1.0 + exp(-a->adapt_rate * l));
}

float
adapter_linear(struct dynkomi_adaptive *a, struct board *b)
{
	/* Figure out how much to adjust the komi based on the game
	 * stage. We just linearly increase/decrease the adaptation
	 * rate for first N moves. */
	if (b->moves > a->adapt_moves)
		return 0;
	if (a->adapt_dir < 0)
		return 1 - (- a->adapt_dir) * b->moves / a->adapt_moves;
	else
		return a->adapt_dir * b->moves / a->adapt_moves;
}

float
uct_dynkomi_adaptive_permove(struct uct_dynkomi *d, struct board *b, struct tree *tree)
{
	struct dynkomi_adaptive *a = d->data;
	if (DEBUGL(3))
		fprintf(stderr, "m %d/%d ekomi %f permove %f/%d\n",
			b->moves, a->lead_moves, tree->extra_komi,
			tree->score.value, tree->score.playouts);
	if (b->moves <= a->lead_moves)
		return board_effective_handicap(b, 7 /* XXX */);

	/* Get lower bound on komi value so that we don't underperform
	 * too much. XXX: We rely on the fact that we don't use dynkomi
	 * as white for now. */
	float min_komi = - a->max_losing_komi;

	/* Perhaps we are adaptive value-based, not score-based? */
	if (a->value_based) {
		/* In that case, we have three zones:
		 * red zone | yellow zone | green zone
		 *        ~45%           ~60%
		 * red zone: reduce komi
		 * yellow zone: do not touch komi
		 * green zone: enlage komi.
		 *
		 * Also, at some point komi will be tuned in such way
		 * that it will be in green zone but increasing it will
		 * be unfeasible. Thus, we have a _latch_ - we will
		 * remember the last komi that has put us into the
		 * red zone, and not use it or go over it. We use the
		 * latch only when giving extra komi, we always want
		 * to try to reduce extra komi we take.
		 *
		 * TODO: Make the latch expire after a while. */
		if (tree->root->u.playouts < TRUSTWORTHY_KOMI_PLAYOUTS)
			return tree->extra_komi;
		float value = tree->root->u.value;
		float extra_komi = tree->extra_komi;
		if (value < a->zone_red) {
			/* Red zone. Take extra komi. */
			if (extra_komi > 0) a->komi_latch = extra_komi;
			extra_komi -= a->score_step; // XXX: we depend on being black
			return extra_komi > min_komi ? extra_komi : min_komi;
		} else if (value < a->zone_green) {
			/* Yellow zone, do nothing. */
			return extra_komi;
		} else {
			/* Green zone. Give extra komi. */
			extra_komi += a->score_step; // XXX: we depend on being black
			return extra_komi < a->komi_latch ? extra_komi : a->komi_latch - 1;
		}
	}

	if (tree->score.playouts < TRUSTWORTHY_KOMI_PLAYOUTS)
		return tree->extra_komi;

	struct move_stats score = tree->score;
	/* Almost-reset tree->score to gather fresh stats. */
	tree->score.playouts = 1;

	/* Look at average score and push extra_komi in that direction. */
	float p = a->adapter(a, b);
	p = a->adapt_base + p * (1 - a->adapt_base);
	if (p > 0.9) p = 0.9; // don't get too eager!
	float extra_komi = tree->extra_komi + p * score.value;
	if (DEBUGL(3))
		fprintf(stderr, "mC %f + %f * %f = %f\n",
			tree->extra_komi, p, score.value, extra_komi);
	return extra_komi > min_komi ? extra_komi : min_komi;
}

float
uct_dynkomi_adaptive_persim(struct uct_dynkomi *d, struct board *b, struct tree *tree, struct tree_node *node)
{
	return tree->extra_komi;
}

struct uct_dynkomi *
uct_dynkomi_init_adaptive(struct uct *u, char *arg, struct board *b)
{
	struct uct_dynkomi *d = calloc(1, sizeof(*d));
	d->uct = u;
	d->permove = uct_dynkomi_adaptive_permove;
	d->persim = uct_dynkomi_adaptive_persim;
	d->done = uct_dynkomi_generic_done;

	struct dynkomi_adaptive *a = calloc(1, sizeof(*a));
	d->data = a;

	if (board_size(b) - 2 >= 19)
		a->lead_moves = 20;
	else
		a->lead_moves = 4; // XXX
	a->max_losing_komi = 10;
	a->adapter = adapter_sigmoid;
	a->adapt_rate = 20;
	a->adapt_phase = 0.5;
	a->adapt_moves = 200;
	a->adapt_dir = -0.5;
	a->adapt_base = 0.2;

	a->zone_red = 0.45;
	a->zone_green = 0.6;
	a->score_step = 2;
	a->komi_latch = -1000;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "lead_moves") && optval) {
				/* Do not adjust komi adaptively for first
				 * N moves. */
				a->lead_moves = atoi(optval);
			} else if (!strcasecmp(optname, "max_losing_komi") && optval) {
				a->max_losing_komi = atof(optval);

			} else if (!strcasecmp(optname, "value_based")) {
				a->value_based = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "zone_red") && optval) {
				a->zone_red = atof(optval);
			} else if (!strcasecmp(optname, "zone_green") && optval) {
				a->zone_green = atof(optval);
			} else if (!strcasecmp(optname, "score_step") && optval) {
				a->score_step = atoi(optval);

			} else if (!strcasecmp(optname, "adapter") && optval) {
				/* Adaptatation method. */
				if (!strcasecmp(optval, "sigmoid")) {
					a->adapter = adapter_sigmoid;
				} else if (!strcasecmp(optval, "linear")) {
					a->adapter = adapter_linear;
				} else {
					fprintf(stderr, "UCT: Invalid adapter %s\n", optval);
					exit(1);
				}
			} else if (!strcasecmp(optname, "adapt_base") && optval) {
				/* Adaptation base rate; see above. */
				a->adapt_base = atof(optval);
			} else if (!strcasecmp(optname, "adapt_rate") && optval) {
				/* Adaptation slope; see above. */
				a->adapt_rate = atof(optval);
			} else if (!strcasecmp(optname, "adapt_phase") && optval) {
				/* Adaptation phase shift; see above. */
				a->adapt_phase = atof(optval);
			} else if (!strcasecmp(optname, "adapt_moves") && optval) {
				/* Adaptation move amount; see above. */
				a->adapt_moves = atoi(optval);
			} else if (!strcasecmp(optname, "adapt_dir") && optval) {
				/* Adaptation direction vector; see above. */
				a->adapt_dir = atof(optval);
			} else {
				fprintf(stderr, "uct: Invalid dynkomi argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	return d;
}
