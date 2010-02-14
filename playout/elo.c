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
 * moves to be played.
 *
 * We use the general pattern classifier that will find the features
 * for us, and external datasets that can be harvested from a set
 * of game records (see the HACKING file for details): patterns.spat
 * as a dictionary of spatial stone configurations, and patterns.gamma
 * with strengths of particular features. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "patternsp.h"
#include "playout.h"
#include "playout/elo.h"
#include "random.h"
#include "tactics.h"
#include "uct/prior.h"

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)


/* Note that the context can be shared by multiple threads! */

struct patternset {
	pattern_spec ps;
	struct pattern_config pc;
	struct features_gamma *fg;
};

struct elo_policy {
	float selfatari;
	struct patternset choose, assess;
};


/* This is the core of the policy - initializes and constructs the
 * probability distribution over the move candidates. */

int
elo_get_probdist(struct playout_policy *p, struct patternset *ps, struct board *b, enum stone to_play, struct probdist *pd)
{
	//struct elo_policy *pp = p->data;
	int moves = 0;

	/* First, assign per-point probabilities. */

	for (int f = 0; f < b->flen; f++) {
		struct move m = { .coord = b->f[f], .color = to_play };

		/* Skip pass (for now)? */
		if (is_pass(m.coord)) {
skip_move:
			probdist_set(pd, f, 0);
			continue;
		}
		//fprintf(stderr, "<%d> %s\n", f, coord2sstr(m.coord, b));

		/* Skip invalid moves. */
		if (!board_is_valid_move(b, &m))
			goto skip_move;

		/* We shall never fill our own single-point eyes. */
		/* XXX: In some rare situations, this prunes the best move:
		 * Bulk-five nakade with eye at 1-1 point. */
		if (board_is_one_point_eye(b, &m.coord, to_play)) {
			goto skip_move;
		}

		moves++;
		/* Each valid move starts with gamma 1. */
		float g = 1.f;

		/* Some easy features: */
		/* XXX: We just disable them for now since we call the
		 * pattern matcher; you need the gammas file. */
#if 0
		if (is_bad_selfatari(b, to_play, m.coord))
			g *= pp->selfatari;
#endif

		/* Match pattern features: */
		struct pattern p;
		pattern_match(&ps->pc, ps->ps, &p, b, &m);
		for (int i = 0; i < p.n; i++) {
			/* Multiply together gammas of all pattern features. */
			float gamma = feature_gamma(ps->fg, &p.f[i], NULL);
			//char buf[256] = ""; feature2str(buf, &p.f[i]);
			//fprintf(stderr, "<%d> %s feat %s gamma %f\n", f, coord2sstr(m.coord, b), buf, gamma);
			g *= gamma;
		}

		probdist_set(pd, f, g);
		//fprintf(stderr, "<%d> %s %f (E %f)\n", f, coord2sstr(m.coord, b), probdist_one(pd, f), pd->items[f]);
	}

	return moves;
}


coord_t
playout_elo_choose(struct playout_policy *p, struct board *b, enum stone to_play)
{
#ifdef BOARD_GAMMA
	struct probdist *pd = &b->prob[to_play - 1];
	/* Make sure ko-prohibited move does not get picked. */
	if (!is_pass(b->ko.coord)) {
		assert(b->ko.color == to_play);
		probdist_set(pd, b->ko.coord, 0);
	}
	/* TODO: FEAT_CONTIGUITY support. */
	/* Pick a move. */
	coord_t c = probdist_pick(pd);
	/* Repair the damage. */
	if (!is_pass(b->ko.coord))
		board_gamma_update(b, b->ko.coord, to_play);
	return c;
#else
	struct elo_policy *pp = p->data;
	float pdi[b->flen]; memset(pdi, 0, sizeof(pdi));
	struct probdist pd = { .n = b->flen, .items = pdi, .total = 0 };
	elo_get_probdist(p, &pp->choose, b, to_play, &pd);
	int f = probdist_pick(&pd);
	return b->f[f];
#endif
}

void
playout_elo_assess(struct playout_policy *p, struct prior_map *map, int games)
{
	struct elo_policy *pp = p->data;
	float pdi[map->b->flen]; memset(pdi, 0, sizeof(pdi));
	struct probdist pd = { .n = map->b->flen, .items = pdi, .total = 0 };

	int moves;
	moves = elo_get_probdist(p, &pp->assess, map->b, map->to_play, &pd);

	/* It is a question how to transform the gamma to won games; we use
	 * a naive approach currently, but not sure how well it works. */
	/* TODO: Try sqrt(p), atan(p)/pi*2. */

	for (int f = 0; f < map->b->flen; f++) {
		coord_t c = map->b->f[f];
		if (!map->consider[c])
			continue;
		add_prior_value(map, c, probdist_one(&pd, f) / probdist_total(&pd), games);
	}
}

void
playout_elo_done(struct playout_policy *p)
{
	struct elo_policy *pp = p->data;
	features_gamma_done(pp->choose.fg);
	features_gamma_done(pp->assess.fg);
}


struct playout_policy *
playout_elo_init(char *arg, struct board *b)
{
	struct playout_policy *p = calloc(1, sizeof(*p));
	struct elo_policy *pp = calloc(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_elo_choose;
	p->assess = playout_elo_assess;
	p->done = playout_elo_done;

	const char *gammafile = features_gamma_filename;
	/* Some defaults based on the table in Remi Coulom's paper. */
	pp->selfatari = 0.06;

	struct pattern_config pc = DEFAULT_PATTERN_CONFIG;
	int xspat = -1;

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
			} else if (!strcasecmp(optname, "gammafile") && optval) {
				/* patterns.gamma by default. We use this,
				 * and need also ${gammafile}f (e.g.
				 * patterns.gammaf) for fast (MC) features. */
				gammafile = strdup(optval);
			} else if (!strcasecmp(optname, "xspat") && optval) {
				/* xspat==0: don't match spatial features
				 * xspat==1: match *only* spatial features */
				xspat = atoi(optval);
			} else {
				fprintf(stderr, "playout-elo: Invalid policy argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	pc.spat_dict = spatial_dict_init(false);

	pp->assess.pc = pc;
	pp->assess.fg = features_gamma_init(&pp->assess.pc, gammafile);
	memcpy(pp->assess.ps, PATTERN_SPEC_MATCHALL, sizeof(pattern_spec));
	for (int i = 0; i < FEAT_MAX; i++)
		if ((xspat == 0 && i == FEAT_SPATIAL) || (xspat == 1 && i != FEAT_SPATIAL))
			pp->assess.ps[i] = 0;

	/* In playouts, we need to operate with much smaller set of features
	 * in order to keep reasonable speed. */
	/* TODO: Configurable. */ /* TODO: Tune. */
	pp->choose.pc = FAST_PATTERN_CONFIG;
	pp->choose.pc.spat_dict = pc.spat_dict;
	char cgammafile[256]; strcpy(stpcpy(cgammafile, gammafile), "f");
	pp->choose.fg = features_gamma_init(&pp->choose.pc, cgammafile);
	memcpy(pp->choose.ps, PATTERN_SPEC_MATCHFAST, sizeof(pattern_spec));
	for (int i = 0; i < FEAT_MAX; i++)
		if ((xspat == 0 && i == FEAT_SPATIAL) || (xspat == 1 && i != FEAT_SPATIAL))
			pp->choose.ps[i] = 0;

#ifdef BOARD_GAMMA
	b->gamma = pp->choose.fg;
#endif

	return p;
}
