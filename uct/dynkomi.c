#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "tactics/util.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/tree.h"


static void
generic_done(struct uct_dynkomi *d)
{
	if (d->data) free(d->data);
	free(d);
}


/* NONE dynkomi strategy - never fiddle with komi values. */

struct uct_dynkomi *
uct_dynkomi_init_none(struct uct *u, char *arg, struct board *b)
{
	struct uct_dynkomi *d = calloc2(1, sizeof(*d));
	d->uct = u;
	d->permove = NULL;
	d->persim = NULL;
	d->done = generic_done;
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
 * at @moves moves. Towards the end of the game the linear compensation
 * becomes zero but we increase the extra komi when winning big. This reduces
 * the number of point-wasting moves and makes the game more enjoyable for humans. */

struct dynkomi_linear {
	int handicap_value[S_MAX];
	int moves[S_MAX];
	bool rootbased;
	/* Increase the extra komi if my win ratio  > green_zone but always
	 * keep extra_komi <= komi_ratchet. komi_ratchet starts infinite but decreases
	 * when we give too much extra komi and this puts us back < orange_zone.
	 * This is meant only to increase the territory margin when playing against
	 * weaker opponents. We never take negative komi when losing. The ratchet helps
	 * avoiding oscillations and keeping us above orange_zone.
	 * To disable the adaptive phase, set green_zone=2. */
	floating_t komi_ratchet;
	floating_t green_zone;
	floating_t orange_zone;
	floating_t drop_step;
};

static floating_t
linear_simple(struct dynkomi_linear *l, struct board *b, enum stone color)
{
	int lmoves = l->moves[color];
	floating_t base_komi = board_effective_handicap(b, l->handicap_value[color]);
	return base_komi * (lmoves - b->moves) / lmoves;
}

static floating_t
linear_permove(struct uct_dynkomi *d, struct board *b, struct tree *tree)
{
	struct dynkomi_linear *l = d->data;
	enum stone color = d->uct->pondering ? tree->root_color : stone_other(tree->root_color);
	int lmoves = l->moves[color];

	if (b->moves < lmoves)
		return linear_simple(l, b, color);

	/* Allow simple adaptation in extreme endgame situations. */

	floating_t extra_komi = floor(tree->extra_komi);

	/* Do not take decisions on unstable value. */
        if (tree->root->u.playouts < GJ_MINGAMES)
		return extra_komi;

	floating_t my_value = tree_node_get_value(tree, 1, tree->root->u.value);
	/*  We normalize komi as in komi_by_value(), > 0 when winning. */
	extra_komi = komi_by_color(extra_komi, color);
	if (extra_komi < 0 && DEBUGL(3))
		fprintf(stderr, "XXX: extra_komi %.3f < 0 (color %s tree ek %.3f)\n", extra_komi, stone2str(color), tree->extra_komi);
	// assert(extra_komi >= 0);
	floating_t orig_komi = extra_komi;

	if (my_value < 0.5 && l->komi_ratchet > 0 && l->komi_ratchet != INFINITY) {
		if (DEBUGL(0))
			fprintf(stderr, "losing %f extra komi %.1f ratchet %.1f -> 0\n",
				my_value, extra_komi, l->komi_ratchet);
		/* Disable dynkomi completely, too dangerous in this game. */
		extra_komi = l->komi_ratchet = 0;
		tree->untrustworthy_tree = true;

	} else if (my_value < l->orange_zone && extra_komi > 0) {
		extra_komi = l->komi_ratchet  = fmax(extra_komi - l->drop_step, 0.0);
		if (extra_komi != orig_komi && DEBUGL(3)) {
			fprintf(stderr, "dropping to %f, extra komi %.1f -> %.1f\n",
				my_value, orig_komi, extra_komi);
			tree->untrustworthy_tree = true;
		}

	} else if (my_value > l->green_zone && extra_komi + 1 <= l->komi_ratchet) {
		extra_komi += 1;
		if (extra_komi != orig_komi && DEBUGL(3))
			fprintf(stderr, "winning %f extra_komi %.1f -> %.1f, ratchet %.1f\n",
				my_value, orig_komi, extra_komi, l->komi_ratchet);
	}
	return komi_by_color(extra_komi, color);
}

static floating_t
linear_persim(struct uct_dynkomi *d, struct board *b, struct tree *tree, struct tree_node *node)
{
	struct dynkomi_linear *l = d->data;
	if (l->rootbased)
		return tree->extra_komi;

	/* We don't reuse computed value from tree->extra_komi,
	 * since we want to use value correct for this node depth.
	 * This also means the values will stay correct after
	 * node promotion. */

	enum stone color = d->uct->pondering ? tree->root_color : stone_other(tree->root_color);
	int lmoves = l->moves[color];
	if (b->moves < lmoves)
		return linear_simple(l, b, color);
	return tree->extra_komi;
}

struct uct_dynkomi *
uct_dynkomi_init_linear(struct uct *u, char *arg, struct board *b)
{
	struct uct_dynkomi *d = calloc2(1, sizeof(*d));
	d->uct = u;
	d->permove = linear_permove;
	d->persim = linear_persim;
	d->done = generic_done;

	struct dynkomi_linear *l = calloc2(1, sizeof(*l));
	d->data = l;

	/* Force white to feel behind and try harder, but not to the
	 * point of resigning immediately in high handicap games.
	 * By move 100 white should still be behind but should have
	 * caught up enough to avoid resigning. */
	int moves = board_large(b) ? 100 : 50;
	if (!board_small(b)) {
		l->moves[S_BLACK] = moves;
		l->moves[S_WHITE] = moves;
	}

	/* The real value of one stone is twice the komi so about 15 points.
	 * But use a lower value to avoid being too pessimistic as black
	 * or too optimistic as white. */
	l->handicap_value[S_BLACK] = 8;
	l->handicap_value[S_WHITE] = 1;

	l->komi_ratchet = INFINITY;
	l->green_zone = 0.85;
	l->orange_zone = 0.8;
	l->drop_step = 4.0;

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
				 * #optval. moves=blackmoves%whitemoves */
				for (int i = S_BLACK; *optval && i <= S_WHITE; i++) {
					l->moves[i] = atoi(optval);
					optval += strcspn(optval, "%");
					if (*optval) optval++;
				}
			} else if (!strcasecmp(optname, "handicap_value") && optval) {
				/* Point value of single handicap stone,
				 * for dynkomi computation. */
				for (int i = S_BLACK; *optval && i <= S_WHITE; i++) {
					l->handicap_value[i] = atoi(optval);
					optval += strcspn(optval, "%");
					if (*optval) optval++;
				}
			} else if (!strcasecmp(optname, "rootbased")) {
				/* If set, the extra komi applied will be
				 * the same for all simulations within a move,
				 * instead of being same for all simulations
				 * within the tree node. */
				l->rootbased = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "green_zone") && optval) {
				/* Increase komi when win ratio is above green_zone */
				l->green_zone = atof(optval);
			} else if (!strcasecmp(optname, "orange_zone") && optval) {
				/* Decrease komi when > 0 and win ratio is below orange_zone */
				l->orange_zone = atof(optval);
			} else if (!strcasecmp(optname, "drop_step") && optval) {
				/* Decrease komi by drop_step points */
				l->drop_step = atof(optval);
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
 * and adjusted throughout a single tree search. */

struct dynkomi_adaptive {
	/* Do not take measured average score into regard for
	 * first @lead_moves - the variance is just too much.
	 * (Instead, we consider the handicap-based komi provided
	 * by linear dynkomi.) */
	int lead_moves;
	/* Maximum komi to pretend the opponent to give. */
	floating_t max_losing_komi;
	/* Game portion at which losing komi is not allowed anymore. */
	floating_t losing_komi_stop;
	/* Turn off dynkomi at the (perceived) closing of the game
	 * (last few moves). */
	bool no_komi_at_game_end;
	/* Alternative game portion determination. */
	bool adapt_aport;
	floating_t (*indicator)(struct uct_dynkomi *d, struct board *b, struct tree *tree, enum stone color);

	/* Value-based adaptation. */
	floating_t zone_red, zone_green;
	int score_step;
	floating_t score_step_byavg; // use portion of average score as increment
	bool use_komi_ratchet;
	bool losing_komi_ratchet; // ratchet even losing komi
	int komi_ratchet_maxage;
	// runtime, not configuration:
	int komi_ratchet_age;
	floating_t komi_ratchet;

	/* Score-based adaptation. */
	floating_t (*adapter)(struct uct_dynkomi *d, struct board *b);
	floating_t adapt_base; // [0,1)
	/* Sigmoid adaptation rate parameter; see below for details. */
	floating_t adapt_phase; // [0,1]
	floating_t adapt_rate; // [1,infty)
	/* Linear adaptation rate parameter. */
	int adapt_moves;
	floating_t adapt_dir; // [-1,1]
};
#define TRUSTWORTHY_KOMI_PLAYOUTS 200

static floating_t
board_game_portion(struct dynkomi_adaptive *a, struct board *b)
{
	if (!a->adapt_aport) {
		int total_moves = b->moves + 2 * board_estimated_moves_left(b);
		return (floating_t) b->moves / total_moves;
	} else {
		int brsize = board_size(b) - 2;
		return 1.0 - (floating_t) b->flen / (brsize * brsize);
	}
}

static floating_t
adapter_sigmoid(struct uct_dynkomi *d, struct board *b)
{
	struct dynkomi_adaptive *a = d->data;
	/* Figure out how much to adjust the komi based on the game
	 * stage. The adaptation rate is 0 at the beginning,
	 * at game stage a->adapt_phase crosses though 0.5 and
	 * approaches 1 at the game end; the slope is controlled
	 * by a->adapt_rate. */
	floating_t game_portion = board_game_portion(a, b);
	floating_t l = game_portion - a->adapt_phase;
	return 1.0 / (1.0 + exp(-a->adapt_rate * l));
}

static floating_t
adapter_linear(struct uct_dynkomi *d, struct board *b)
{
	struct dynkomi_adaptive *a = d->data;
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

static floating_t
komi_by_score(struct uct_dynkomi *d, struct board *b, struct tree *tree, enum stone color)
{
	struct dynkomi_adaptive *a = d->data;
	if (d->score.playouts < TRUSTWORTHY_KOMI_PLAYOUTS)
		return tree->extra_komi;

	struct move_stats score = d->score;
	/* Almost-reset tree->score to gather fresh stats. */
	d->score.playouts = 1;

	/* Look at average score and push extra_komi in that direction. */
	floating_t p = a->adapter(d, b);
	p = a->adapt_base + p * (1 - a->adapt_base);
	if (p > 0.9) p = 0.9; // don't get too eager!
	floating_t extra_komi = tree->extra_komi + p * score.value;
	if (DEBUGL(3))
		fprintf(stderr, "mC += %f * %f\n", p, score.value);
	return extra_komi;
}

static floating_t
komi_by_value(struct uct_dynkomi *d, struct board *b, struct tree *tree, enum stone color)
{
	struct dynkomi_adaptive *a = d->data;
	if (d->value.playouts < TRUSTWORTHY_KOMI_PLAYOUTS)
		return tree->extra_komi;

	struct move_stats value = d->value;
	/* Almost-reset tree->value to gather fresh stats. */
	d->value.playouts = 1;
	/* Correct color POV. */
	if (color == S_WHITE)
		value.value = 1 - value.value;

	/* We have three "value zones":
	 * red zone | yellow zone | green zone
	 *        ~45%           ~60%
	 * red zone: reduce komi
	 * yellow zone: do not touch komi
	 * green zone: enlage komi.
	 *
	 * Also, at some point komi will be tuned in such way
	 * that it will be in green zone but increasing it will
	 * be unfeasible. Thus, we have a _ratchet_ - we will
	 * remember the last komi that has put us into the
	 * red zone, and not use it or go over it. We use the
	 * ratchet only when giving extra komi, we always want
	 * to try to reduce extra komi we take.
	 *
	 * TODO: Make the ratchet expire after a while. */

	/* We use komi_by_color() first to normalize komi
	 * additions/subtractions, then apply it again on
	 * return value to restore original komi parity. */
	/* Positive extra_komi means that we are _giving_
	 * komi (winning), negative extra_komi is _taking_
	 * komi (losing). */
	floating_t extra_komi = komi_by_color(tree->extra_komi, color);
	int score_step_red = -a->score_step;
	int score_step_green = a->score_step;

	if (a->score_step_byavg != 0) {
		struct move_stats score = d->score;
		/* Almost-reset tree->score to gather fresh stats. */
		d->score.playouts = 1;
		/* Correct color POV. */
		if (color == S_WHITE)
			score.value = - score.value;
		if (score.value > 0)
			score_step_green = round(score.value * a->score_step_byavg);
		else
			score_step_red = round(-score.value * a->score_step_byavg);
		if (score_step_green < 0 || score_step_red > 0) {
			/* The steps are in bad direction - keep still. */
			return komi_by_color(extra_komi, color);
		}
	}

	/* Wear out the ratchet. */
	if (a->use_komi_ratchet && a->komi_ratchet_maxage > 0) {
		a->komi_ratchet_age += value.playouts;
		if (a->komi_ratchet_age > a->komi_ratchet_maxage) {
			a->komi_ratchet = 1000;
			a->komi_ratchet_age = 0;
		}
	}

	if (value.value < a->zone_red) {
		/* Red zone. Take extra komi. */
		if (DEBUGL(3))
			fprintf(stderr, "[red] %f, step %d | komi ratchet %f age %d/%d -> %f\n",
				value.value, score_step_red, a->komi_ratchet, a->komi_ratchet_age, a->komi_ratchet_maxage, extra_komi);
		if (a->losing_komi_ratchet || extra_komi > 0) {
			a->komi_ratchet = extra_komi;
			a->komi_ratchet_age = 0;
		}
		extra_komi += score_step_red;
		return komi_by_color(extra_komi, color);

	} else if (value.value < a->zone_green) {
		/* Yellow zone, do nothing. */
		return komi_by_color(extra_komi, color);

	} else {
		/* Green zone. Give extra komi. */
		if (DEBUGL(3))
			fprintf(stderr, "[green] %f, step %d | komi ratchet %f age %d/%d\n",
				value.value, score_step_green, a->komi_ratchet, a->komi_ratchet_age, a->komi_ratchet_maxage);
		extra_komi += score_step_green;
		if (a->use_komi_ratchet && extra_komi >= a->komi_ratchet)
			extra_komi = a->komi_ratchet - 1;
		return komi_by_color(extra_komi, color);
	}
}

static floating_t
bounded_komi(struct dynkomi_adaptive *a, struct board *b,
             enum stone color, floating_t komi, floating_t max_losing_komi)
{
	/* At the end of game, disallow losing komi. */
	if (komi_by_color(komi, color) < 0
	    && board_game_portion(a, b) > a->losing_komi_stop)
		return 0;

	/* Get lower bound on komi we take so that we don't underperform
	 * too much. */
	floating_t min_komi = komi_by_color(- max_losing_komi, color);

	if (komi_by_color(komi - min_komi, color) > 0)
		return komi;
	else
		return min_komi;
}

static floating_t
adaptive_permove(struct uct_dynkomi *d, struct board *b, struct tree *tree)
{
	struct dynkomi_adaptive *a = d->data;
	enum stone color = stone_other(tree->root_color);

	/* We do not use extra komi at the game end - we are not
	 * to fool ourselves at this point. */
	if (a->no_komi_at_game_end && board_estimated_moves_left(b) <= MIN_MOVES_LEFT) {
		tree->use_extra_komi = false;
		return 0;
	}

	if (DEBUGL(4))
		fprintf(stderr, "m %d/%d ekomi %f permove %f/%d\n",
			b->moves, a->lead_moves, tree->extra_komi,
			d->score.value, d->score.playouts);

	if (b->moves <= a->lead_moves)
		return bounded_komi(a, b, color,
		                    board_effective_handicap(b, 7 /* XXX */),
		                    a->max_losing_komi);

	floating_t komi = a->indicator(d, b, tree, color);
	if (DEBUGL(4))
		fprintf(stderr, "dynkomi: %f -> %f\n", tree->extra_komi, komi);
	return bounded_komi(a, b, color, komi, a->max_losing_komi);
}

static floating_t
adaptive_persim(struct uct_dynkomi *d, struct board *b, struct tree *tree, struct tree_node *node)
{
	return tree->extra_komi;
}

struct uct_dynkomi *
uct_dynkomi_init_adaptive(struct uct *u, char *arg, struct board *b)
{
	struct uct_dynkomi *d = calloc2(1, sizeof(*d));
	d->uct = u;
	d->permove = adaptive_permove;
	d->persim = adaptive_persim;
	d->done = generic_done;

	struct dynkomi_adaptive *a = calloc2(1, sizeof(*a));
	d->data = a;

	a->lead_moves = board_large(b) ? 20 : 4; // XXX
	a->max_losing_komi = 30;
	a->losing_komi_stop = 1.0f;
	a->no_komi_at_game_end = true;
	a->indicator = komi_by_value;

	a->adapter = adapter_sigmoid;
	a->adapt_rate = -18;
	a->adapt_phase = 0.65;
	a->adapt_moves = 200;
	a->adapt_dir = -0.5;

	a->zone_red = 0.45;
	a->zone_green = 0.50;
	a->score_step = 1;
	a->use_komi_ratchet = true;
	a->komi_ratchet_maxage = 0;
	a->komi_ratchet = 1000;

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
			} else if (!strcasecmp(optname, "losing_komi_stop") && optval) {
				a->losing_komi_stop = atof(optval);
			} else if (!strcasecmp(optname, "no_komi_at_game_end")) {
				a->no_komi_at_game_end = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "indicator")) {
				/* Adaptatation indicator - how to decide
				 * the adaptation rate and direction. */
				if (!strcasecmp(optval, "value")) {
					/* Winrate w/ komi so far. */
					a->indicator = komi_by_value;
				} else if (!strcasecmp(optval, "score")) {
					/* Expected score w/ current komi. */
					a->indicator = komi_by_score;
				} else {
					fprintf(stderr, "UCT: Invalid indicator %s\n", optval);
					exit(1);
				}

				/* value indicator settings */
			} else if (!strcasecmp(optname, "zone_red") && optval) {
				a->zone_red = atof(optval);
			} else if (!strcasecmp(optname, "zone_green") && optval) {
				a->zone_green = atof(optval);
			} else if (!strcasecmp(optname, "score_step") && optval) {
				a->score_step = atoi(optval);
			} else if (!strcasecmp(optname, "score_step_byavg") && optval) {
				a->score_step_byavg = atof(optval);
			} else if (!strcasecmp(optname, "use_komi_ratchet")) {
				a->use_komi_ratchet = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "losing_komi_ratchet")) {
				a->losing_komi_ratchet = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "komi_ratchet_age") && optval) {
				a->komi_ratchet_maxage = atoi(optval);

				/* score indicator settings */
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
			} else if (!strcasecmp(optname, "adapt_aport")) {
				a->adapt_aport = !optval || atoi(optval);
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
