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

//#define DEBUG
#include "board.h"
#include "debug.h"
#include "fixp.h"
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
	playout_elo_callbackp callback; void *callback_data;

	enum {
		EAV_TOTAL,
		EAV_BEST,
	} assess_eval;
	enum {
		EAT_LINEAR,
	} assess_transform;
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
			probdist_set(pd, m.coord, 0);
			continue;
		}
		if (PLDEBUGL(7))
			fprintf(stderr, "<%d> %s\n", f, coord2sstr(m.coord, b));

		/* Skip invalid moves. */
		if (!board_is_valid_move(b, &m))
			goto skip_move;

		/* We shall never fill our own single-point eyes. */
		/* XXX: In some rare situations, this prunes the best move:
		 * Bulk-five nakade with eye at 1-1 point. */
		if (board_is_one_point_eye(b, m.coord, to_play)) {
			goto skip_move;
		}

		moves++;
		/* Each valid move starts with gamma 1. */
		double g = 1.f;

		/* Some easy features: */
		/* XXX: We just disable them for now since we call the
		 * pattern matcher; you need the gammas file. */
#if 0
		if (is_bad_selfatari(b, to_play, m.coord))
			g *= pp->selfatari;
#endif

		/* Match pattern features: */
		struct pattern pat;
		pattern_match(&ps->pc, ps->ps, &pat, b, &m);
		for (int i = 0; i < pat.n; i++) {
			/* Multiply together gammas of all pattern features. */
			double gamma = feature_gamma(ps->fg, &pat.f[i], NULL);
			if (PLDEBUGL(7)) {
				char buf[256] = ""; feature2str(buf, &pat.f[i]);
				fprintf(stderr, "<%d> %s feat %s gamma %f\n", f, coord2sstr(m.coord, b), buf, gamma);
			}
			g *= gamma;
		}

		probdist_set(pd, m.coord, double_to_fixp(g));
		if (PLDEBUGL(7))
			fprintf(stderr, "<%d> %s %f (E %f)\n", f, coord2sstr(m.coord, b), fixp_to_double(probdist_one(pd, m.coord)), g);
	}

	return moves;
}


struct lprobdist {
	int n;
#define LPD_MAX 8
	coord_t coords[LPD_MAX];
	fixp_t items[LPD_MAX];
	fixp_t total;
	
	/* Backups of original totals for restoring. */
	fixp_t btotal;
	fixp_t browtotals_v[10];
	int browtotals_i[10];
	int browtotals_n;
};

#ifdef BOARD_GAMMA

static void
elo_check_probdist(struct playout_policy *p, struct board *b, enum stone to_play, struct probdist *pd, int *ignores, struct lprobdist *lpd, coord_t lc)
{
#if 0
#define PROBDIST_EPSILON double_to_fixp(0.01)
	struct elo_policy *pp = p->data;
	if (pd->total == 0)
		return;

	/* Compare to the manually created distribution. */
	/* XXX: This is now broken if callback is used. */

	probdist_alloca(pdx, b);
	elo_get_probdist(p, &pp->choose, b, to_play, &pdx);
	for (int i = 0; i < b->flen; i++) {
		coord_t c = b->f[i];
		if (is_pass(c)) continue;
		if (c == b->ko.coord) continue;
		fixp_t val = pd->items[c];
		if (!is_pass(lc) && coord_is_8adjecent(lc, c, b))
			for (int j = 0; j < lpd->n; j++)
				if (lpd->coords[j] == c) {
					val = lpd->items[j];
					probdist_mute(&pdx, c);

				}
		if (abs(pdx.items[c] - val) < PROBDIST_EPSILON)
			continue;
		printf("[%s %d] manual %f board %f (base %f) ", coord2sstr(c, b), b->pat3[c], fixp_to_double(pdx.items[c]), fixp_to_double(val), fixp_to_double(pd->items[c]));
		board_gamma_update(b, c, to_play);
		printf("plainboard %f\n", fixp_to_double(pd->items[c]));
		assert(0);
	}
	for (int r = 0; r < board_size(b); r++) {
		if (abs(pdx.rowtotals[r] - pd->rowtotals[r]) < PROBDIST_EPSILON)
			continue;
		fprintf(stderr, "row %d: manual %f board %f\n", r, fixp_to_double(pdx.rowtotals[r]), fixp_to_double(pd->rowtotals[r]));
		assert(0);
	}
	assert(abs(pdx.total - pd->total) < PROBDIST_EPSILON);
#undef PROBDIST_EPSILON
#endif
}

coord_t
playout_elo_choose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct elo_policy *pp = p->data;
	/* The base board probdist. */
	struct probdist *pd = &b->prob[to_play - 1];
	/* The list of moves we do not consider in pd. */
	int ignores[10]; int ignores_n = 0;
	/* The list of local moves; we consider these separately. */
	struct lprobdist lpd = { .n = 0, .total = 0, .btotal = pd->total, .browtotals_n = 0 };

	/* The engine might want to adjust our probdist. */
	if (pp->callback)
		pp->callback(pp->callback_data, b, to_play, pd);

	if (PLDEBUGL(5)) {
		fprintf(stderr, "pd total pre %f lpd %f\n", fixp_to_double(pd->total), fixp_to_double(lpd.total));
	}

#define ignore_move(c_) do { \
	ignores[ignores_n++] = c_; \
	if (ignores_n > 1 && ignores[ignores_n - 1] < ignores[ignores_n - 2]) { \
		/* Keep ignores[] sorted. We abuse the fact that we know \
		 * only one item can be out-of-order. */ \
		coord_t cc = ignores[ignores_n - 2]; \
		ignores[ignores_n - 2] = ignores[ignores_n - 1]; \
		ignores[ignores_n - 1] = cc; \
	} \
	int rowi = coord_y(c_, pd->b); \
	lpd.browtotals_i[lpd.browtotals_n] = rowi; \
	lpd.browtotals_v[lpd.browtotals_n++] = pd->rowtotals[rowi]; \
	probdist_mute(pd, c_); \
	if (PLDEBUGL(6)) \
		fprintf(stderr, "ignored move %s(%f) => tot pd %f lpd %f\n", coord2sstr(c_, pd->b), fixp_to_double(pd->items[c_]), fixp_to_double(pd->total), fixp_to_double(lpd.total)); \
} while (0)

	/* Make sure ko-prohibited move does not get picked. */
	if (!is_pass(b->ko.coord)) {
		assert(b->ko.color == to_play);
		ignore_move(b->ko.coord);
	}

	/* Contiguity detection. */
	if (!is_pass(b->last_move.coord)) {
		foreach_8neighbor(b, b->last_move.coord) {
			if (c == b->ko.coord)
				continue; // already ignored
			if (board_at(b, c) != S_NONE) {
				assert(probdist_one(pd, c) == 0);
				continue;
			}
			ignore_move(c);

			fixp_t val = double_to_fixp(fixp_to_double(probdist_one(pd, c)) * b->gamma->gamma[FEAT_CONTIGUITY][1]);
			lpd.coords[lpd.n] = c;
			lpd.items[lpd.n++] = val;
			lpd.total += val;
		} foreach_8neighbor_end;
	}

	ignores[ignores_n] = pass;
	if (PLDEBUGL(5))
		fprintf(stderr, "pd total post %f lpd %f\n", fixp_to_double(pd->total), fixp_to_double(lpd.total));

	/* Verify sanity, possibly. */
	elo_check_probdist(p, b, to_play, pd, ignores, &lpd, b->last_move.coord);

	/* Pick a move. */
	coord_t c = pass;
	fixp_t stab = fast_irandom(lpd.total + pd->total);
	if (PLDEBUGL(5))
		fprintf(stderr, "stab %f / (%f + %f)\n", fixp_to_double(stab), fixp_to_double(lpd.total), fixp_to_double(pd->total));
	if (stab < lpd.total) {
		/* Local probdist. */
		if (PLDEBUGL(6)) {
			/* Some debug prints. */
			fixp_t tot = 0;
			for (int i = 0; i < lpd.n; i++) {
				tot += lpd.items[i];
				struct pattern p;
				struct move m = { .color = to_play, .coord = lpd.coords[i] };
				if (board_at(b, m.coord) != S_NONE) {
					assert(lpd.items[i] == 0);
					continue;
				}
				pattern_match(&pp->choose.pc, pp->choose.ps, &p, b, &m);
				char s[256] = ""; pattern2str(s, &p);
				fprintf(stderr, "coord %s <%f> [tot %f] %s (p3:%d)\n",
					coord2sstr(lpd.coords[i], b), fixp_to_double(lpd.items[i]),
					fixp_to_double(tot), s,
					pattern3_by_spatial(pp->choose.pc.spat_dict, b->pat3[lpd.coords[i]]));
			}
		}
		for (int i = 0; i < lpd.n; i++) {
			if (stab <= lpd.items[i]) {
				c = lpd.coords[i];
				break;
			}
			stab -= lpd.items[i];
		}
		if (is_pass(c)) {
			fprintf(stderr, "elo: local overstab [%f]\n", fixp_to_double(stab));
			abort();
		}

	} else if (pd->total > 0) {
		/* Global probdist. */
		/* XXX: We re-stab inside. */
		c = probdist_pick(pd, ignores);

	} else {
		if (PLDEBUGL(5))
			fprintf(stderr, "ding!\n");
		c = pass;
	}

	/* Repair the damage. */
	if (pp->callback) {
		/* XXX: Do something less horribly inefficient
		 * than just recomputing the whole pd. */
		pd->total = 0;
		for (int i = 0; i < board_size(pd->b); i++)
			pd->rowtotals[i] = 0;
		for (int i = 0; i < b->flen; i++) {
			pd->items[b->f[i]] = 0;
			board_gamma_update(b, b->f[i], to_play);
		}
		assert(pd->total == lpd.btotal);

	} else {
		pd->total = lpd.btotal;
		/* If we touched a row multiple times (and we sure will),
		 * the latter value is obsolete; but since we go through
		 * the backups in reverse order, all is good. */
		for (int j = lpd.browtotals_n - 1; j >= 0; j--)
			pd->rowtotals[lpd.browtotals_i[j]] = lpd.browtotals_v[j];
	}
	return c;
}

#else

coord_t
playout_elo_choose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct elo_policy *pp = p->data;
	probdist_alloca(pd, b);
	elo_get_probdist(p, &pp->choose, b, to_play, &pd);
	if (pp->callback)
		pp->callback(pp->callback_data, b, to_play, &pd);
	if (pd.total == 0)
		return pass;
	int ignores[1] = { pass };
	coord_t c = probdist_pick(&pd, ignores);
	return c;
}

#endif

void
playout_elo_assess(struct playout_policy *p, struct prior_map *map, int games)
{
	struct elo_policy *pp = p->data;
	probdist_alloca(pd, map->b);

	int moves;
	moves = elo_get_probdist(p, &pp->assess, map->b, map->to_play, &pd);

	/* It is a question how to transform the gamma to won games; we use
	 * a naive approach currently, but not sure how well it works. */
	/* TODO: Try sqrt(p), atan(p)/pi*2. */

	double pd_best = 0;
	if (pp->assess_eval == EAV_BEST) {
		for (int f = 0; f < map->b->flen; f++) {
			double pd_one = fixp_to_double(probdist_one(&pd, map->b->f[f]));
			if (pd_one > pd_best)
				pd_best = pd_one;
		}
	}
	double pd_total = fixp_to_double(probdist_total(&pd));

	for (int f = 0; f < map->b->flen; f++) {
		coord_t c = map->b->f[f];
		if (!map->consider[c])
			continue;

		double pd_one = fixp_to_double(probdist_one(&pd, c));
		double val = 0;
		switch (pp->assess_eval) {
		case EAV_TOTAL:
			val = pd_one / pd_total;
			break;
		case EAV_BEST:
			val = pd_one / pd_best;
			break;
		default:
			assert(0);
		}

		switch (pp->assess_transform) {
		case EAT_LINEAR:
			val = val;
			break;
		default:
			assert(0);
		}

		add_prior_value(map, c, val, games);
	}
}

void
playout_elo_done(struct playout_policy *p)
{
	struct elo_policy *pp = p->data;
	features_gamma_done(pp->choose.fg);
	features_gamma_done(pp->assess.fg);
}


void
playout_elo_callback(struct playout_policy *p, playout_elo_callbackp callback, void *data)
{
	struct elo_policy *pp = p->data;
	pp->callback = callback;
	pp->callback_data = data;
}

struct playout_policy *
playout_elo_init(char *arg, struct board *b)
{
	struct playout_policy *p = calloc2(1, sizeof(*p));
	struct elo_policy *pp = calloc2(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_elo_choose;
	p->assess = playout_elo_assess;
	p->done = playout_elo_done;

	const char *gammafile = features_gamma_filename;
	/* Some defaults based on the table in Remi Coulom's paper. */
	pp->selfatari = 0.06;

	struct pattern_config pc = DEFAULT_PATTERN_CONFIG;
	int xspat = -1;
	bool precise_selfatari = false;

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
			} else if (!strcasecmp(optname, "precisesa")) {
				/* Use precise self-atari detection within
				 * fast patterns. */
				precise_selfatari = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "gammafile") && optval) {
				/* patterns.gamma by default. We use this,
				 * and need also ${gammafile}f (e.g.
				 * patterns.gammaf) for fast (MC) features. */
				gammafile = strdup(optval);
			} else if (!strcasecmp(optname, "xspat") && optval) {
				/* xspat==0: don't match spatial features
				 * xspat==1: match *only* spatial features */
				xspat = atoi(optval);
			} else if (!strcasecmp(optname, "assess_eval") && optval) {
				/* Evaluation method for prior node value
				 * assessment. */
				if (!strcasecmp(optval, "total")) {
					/* Proportion prob/totprob. */
					pp->assess_eval = EAV_TOTAL;
				} else if (!strcasecmp(optval, "best")) {
					/* Proportion prob/bestprob. */
					pp->assess_eval = EAV_BEST;
				} else {
					fprintf(stderr, "playout-elo: Invalid eval mode %s\n", optval);
					exit(1);
				}
			} else if (!strcasecmp(optname, "assess_transform") && optval) {
				/* Transformation of evaluation for prior
				 * node value assessment. */
				if (!strcasecmp(optval, "linear")) {
					/* No additional transformation. */
					pp->assess_transform = EAT_LINEAR;
				} else {
					fprintf(stderr, "playout-elo: Invalid eval mode %s\n", optval);
					exit(1);
				}
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
	if (precise_selfatari) {
		pp->choose.ps[FEAT_SELFATARI] &= ~(1<<PF_SELFATARI_STUPID);
		pp->choose.ps[FEAT_SELFATARI] |= (1<<PF_SELFATARI_SMART);
	}
	board_gamma_set(b, pp->choose.fg, precise_selfatari);

	return p;
}
