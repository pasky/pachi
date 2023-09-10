#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern/pattern.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"
#include "engine.h"

prob_dict_t    *prob_dict = NULL;

void
prob_dict_init(char *filename)
{
	assert(!prob_dict);
	if (!filename)  filename = "patterns_mm.gamma";
	FILE *f = fopen_data_file(filename, "r");
	if (!f)
		die("Pattern file %s missing, aborting.\n", filename);

	int gammas = pattern_gammas();
	prob_dict = calloc2(1, prob_dict_t);
	prob_dict->gamma_table   = calloc2(gammas, floating_t);
	prob_dict->feature_table = calloc2(gammas, feature_t);

	/* All gammas = -1.0 (unset) */
	for (int i = 0; i < gammas; i++)
		prob_dict->gamma_table[i] = -1.0;

	/* Read in gammas */
	int n = 0;
	char sbuf[1024];
	while (fgets(sbuf, sizeof(sbuf), f)) {
		char *buf = sbuf;

		if (buf[0] == '#')   /* Comment */
			continue;
		
		while (isspace(*buf))  buf++;
		float gamma = strtof(buf, &buf);
		
		while (isspace(*buf)) buf++;		
		pattern_t p;  memset(&p, 0, sizeof(p));
		feature_t *f = &p.f[0];
		str2pattern(buf, &p);
		assert(p.n == 1);	/* One gamma per feature, please ! */

		int i = feature_gamma_number(f);
		assert(i < pattern_gammas());		/* Bad patterns.spat / patterns.prob ? */
		if (feature_has_gamma(f))
			die("%s: multiple gammas for feature %s\n", filename, pattern2sstr(&p));
		
		prob_dict->gamma_table[i] = gamma;
		prob_dict->feature_table[i] = *f;

		n++;
	}

	fclose(f);
	if (DEBUGL(1))  fprintf(stderr, "Loaded %d gammas.\n", n);
}

void
prob_dict_done()
{
	if (!prob_dict)  return;

	free(prob_dict->gamma_table);
	free(prob_dict->feature_table);
	free(prob_dict);
	prob_dict = NULL;
}


/*****************************************************************************/
/* Low-level pattern rating */

/* Do we have a gamma for that feature ? */
bool
feature_has_gamma(feature_t *f)
{
	int i = feature_gamma_number(f);
	return (feature_eq(f, &prob_dict->feature_table[i]) &&
		prob_dict->gamma_table[i] != -1);
}

/* Lookup gamma for that feature. */
static floating_t
feature_gamma(feature_t *f)
{
	/* Not checking feature_eq(f, prob_dict->feature_table[i]),
	 * we should be properly initialized at this stage. */
	int i = feature_gamma_number(f);
	return prob_dict->gamma_table[i];
}

/* Return probability associated with given pattern. */
static floating_t
pattern_gamma(pattern_t *p)
{
	floating_t gammas = 1;
	for (int i = 0; i < p->n; i++)
		gammas *= feature_gamma(&p->f[i]);
	return gammas;
}

/* Print pattern features' gamma details in @buf */
void
dump_gammas(strbuf_t *buf, pattern_t *p)
{
	const char *head = "";
	floating_t gamma = pattern_gamma(p);
	sbprintf(buf, "%.2f = ", gamma);
	
	for (int i = 0; i < p->n; i++) {
		feature_t *f = &p->f[i];
		sbprintf(buf, "%s(%s) %.2f ", head, feature2sstr(f), feature_gamma(f));
		head = "* ";
		continue;
	}
}


/*****************************************************************************/
/* Move rating */

static void
rescale_probs(board_t *b, floating_t *probs, floating_t total)
{
	for (int f = 0; f < b->flen; f++)
		if (!isnan(probs[f]))
			probs[f] /= total;
}

static floating_t
pattern_rate_move_full(board_t *b, move_t *m, pattern_t *pat,
		       pattern_context_t *ct, bool locally)
{
	floating_t prob = NAN;

	if (is_pass(m->coord))	return prob;
	if (!board_is_valid_play_no_suicide(b, m->color, m->coord)) return prob;

	pattern_match(b, m, pat, ct, locally);
	prob = pattern_gamma(pat);
	
	//if (DEBUGL(5)) {
	//	char buf[256]; pattern2str(buf, pat);
	//	fprintf(stderr, "=> move %s pattern %s prob %.3f\n", coord2sstr(m->coord), buf, prob);
	//}
	return prob;
}

static floating_t
pattern_rate_move_vanilla(board_t *b, move_t *m, pattern_t *pat, pattern_context_t *ct)
{
	floating_t prob = NAN;

	if (is_pass(m->coord))	return prob;
	if (!board_is_valid_play_no_suicide(b, m->color, m->coord)) return prob;

	pattern_match_vanilla(b, m, pat, ct);
	prob = pattern_gamma(pat);
	
	//if (DEBUGL(5)) {
	//	char buf[256]; pattern2str(buf, pat);
	//	fprintf(stderr, "=> move %s pattern %s prob %.3f\n", coord2sstr(m->coord), buf, prob);
	//}
	return prob;
}

static floating_t
pattern_rate_move(board_t *b, move_t *m, pattern_context_t *ct, bool locally)
{
	pattern_t pat;
	return pattern_rate_move_full(b, m, &pat, ct, locally);
}

static floating_t
pattern_max_rating_full(board_t *b, enum stone color, pattern_t *pats, floating_t *probs,
			pattern_context_t *ct, bool locally)
{
	floating_t max = -10000000;
	floating_t total = 0;
	for (int f = 0; f < b->flen; f++) {
		move_t m = move(b->f[f], color);
		probs[f] = pattern_rate_move_full(b, &m, &pats[f], ct, locally);
		if (!isnan(probs[f])) {  total += probs[f];  max = MAX(probs[f], max);  }
	}

	rescale_probs(b, probs, total);
	total = 1.0;
	
	return max;
}

static floating_t
pattern_max_rating(board_t *b, enum stone color, floating_t *probs,
		   pattern_context_t *ct, bool locally)
{
	floating_t max = -100000;
	floating_t total = 0;
	for (int f = 0; f < b->flen; f++) {
		move_t m = move(b->f[f], color);
		probs[f] = pattern_rate_move(b, &m, ct, locally);
		if (!isnan(probs[f])) {  total += probs[f];  max = MAX(max, probs[f]);  }
	}

	rescale_probs(b, probs, total);
	total = 1.0;

	return max;
}

#define LOW_PATTERN_RATING 6.0

/* Save patterns for each move as well. */
void
pattern_rate_moves_full(board_t *b, enum stone color,
			pattern_t *pats, floating_t *probs,
			pattern_context_t *ct)
{
#ifdef PATTERN_FEATURE_STATS
	pattern_stats_new_position();
#endif

	/* Try local moves first. */
	floating_t max = pattern_max_rating_full(b, color, pats, probs, ct, true);

	/* Nothing big matches ? Try again ignoring distance so we get good tenuki moves. */
	if (max < LOW_PATTERN_RATING)
		pattern_max_rating_full(b, color, pats, probs, ct, false);
}

void
pattern_rate_moves(board_t *b, enum stone color, floating_t *probs, pattern_context_t *ct)
{
#ifdef PATTERN_FEATURE_STATS
	pattern_stats_new_position();
#endif

	/* Try local moves first. */
	floating_t max = pattern_max_rating(b, color, probs, ct, true);

	/* Nothing big matches ? Try again ignoring distance so we get good tenuki moves.
	 * (Looks terribly inefficient but this gets hit so rarely it's not worth bothering) */
	if (max < LOW_PATTERN_RATING)
		pattern_max_rating(b, color, probs, ct, false);
}

/* For testing purposes: no prioritized features, check every feature. */
void
pattern_rate_moves_vanilla(board_t *b, enum stone color,
			   pattern_t *pats, floating_t *probs,
			   pattern_context_t *ct)
{
	floating_t total = 0;
	for (int f = 0; f < b->flen; f++) {
		move_t m = move(b->f[f], color);
		probs[f] = pattern_rate_move_vanilla(b, &m, &pats[f], ct);
		if (!isnan(probs[f])) {  total += probs[f];  }
	}
	
	rescale_probs(b, probs, total);
	total = 1.0;
}

bool
pattern_matching_locally(board_t *b, enum stone color, pattern_context_t *ct)
{
	floating_t probs[b->flen];
	floating_t max = pattern_max_rating(b, color, probs, ct, true);
	return (max >= LOW_PATTERN_RATING);
}

void
print_pattern_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest)
{
	int cols = best_moves_print(b, "patterns = ", best_c, nbest);

	fprintf(stderr, "%*s[ ", cols, "");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3i ", (int)(best_r[i] * 100));
	fprintf(stderr, "]\n");
}

void
get_pattern_best_moves(board_t *b, floating_t *probs, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}
	
	for (int f = 0; f < b->flen; f++)
		if (!isnan(probs[f]))
			best_moves_add(b->f[f], probs[f], best_c, best_r, nbest);
}
