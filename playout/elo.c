/* Playout player based on probability distribution generated over
 * the available moves. */

/* We use the ELO-based (Coulom, 2007) approach, where each board
 * feature (matched pattern, self-atari, capture, MC owner?, ...)
 * is pre-assigned "playing strength" (gamma).
 *
 * Then, the problem of choosing a move is basically a team
 * competition in ELO terms - each spot is represented by a team
 * of features appearing there; the team gamma is product of feature
 * gammas. The team gammas make for a probability distribution of
 * moves to be played. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "playout.h"
#include "playout/elo.h"
#include "random.h"
#include "tactics.h"
#include "uct/prior.h"

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)


/* Note that the context can be shared by multiple threads! */

struct elo_policy {
	float selfatari;
};


/* This is the core of the policy - initializes and constructs the
 * probability distribution over the move candidates. */

void
elo_get_probdist(struct playout_policy *p, struct board *b, enum stone to_play, struct probdist *pd)
{
	struct elo_policy *pp = p->data;

	probdist_init(pd, board_size2(b));

	/* First, assign per-point probabilities. */

	foreach_point(b) {
		struct move m = { .coord = c, .color = to_play };

		/* Skip invalid moves. */
		if (!board_is_valid_move(b, &m))
			continue;

		/* We shall never fill our own single-point eyes. */
		/* XXX: In some rare situations, this prunes the best move:
		 * Bulk-five nakade with eye at 1-1 point. */
		if (board_is_one_point_eye(b, &c, to_play)) {
			continue;
		}

		/* Each valid move starts with gamma 1. */
		probdist_add(pd, c, 1.f);

		/* Some easy features: */

		if (is_bad_selfatari(b, to_play, c))
			probdist_mul(pd, c, pp->selfatari);

		/* TODO: Some more actual heuristics! */
	} foreach_point_end;

	/* TODO: per-group probabilities. */
}


coord_t
playout_elo_choose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct probdist pd;
	elo_get_probdist(p, b, to_play, &pd);

	return probdist_pick(&pd);
}

void
playout_elo_assess(struct playout_policy *p, struct prior_map *map, int games)
{
	struct probdist pd;
	elo_get_probdist(p, map->b, map->to_play, &pd);

	/* It is a question how to transform the gamma to won games;
	 * we use a naive approach and just pass the gamma itself as
	 * value. XXX: We hope nothing breaks, since often gamma>1. */
	/* TODO: Try sqrt(p), atan(p)/pi*2. */

	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		add_prior_value(map, c, pd.moves[c], games);
	} foreach_point_end;

	probdist_done(&pd);
}


struct playout_policy *
playout_elo_init(char *arg)
{
	struct playout_policy *p = calloc(1, sizeof(*p));
	struct elo_policy *pp = calloc(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_elo_choose;
	p->assess = playout_elo_assess;

	/* Some defaults based on the table in Remi Coulom's paper. */
	pp->selfatari = 0.06;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "selfatari") && optval) {
				pp->selfatari = atof(optval);
			} else {
				fprintf(stderr, "playout-elo: Invalid policy argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	return p;
}
