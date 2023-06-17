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

	prob_dict = calloc2(1, prob_dict_t);
	prob_dict->table = calloc2(spat_dict->nspatials + 1, pattern_prob_t*);

	int i = 0;
	char sbuf[1024];
	while (fgets(sbuf, sizeof(sbuf), f)) {
		pattern_prob_t *pb = calloc2(1, pattern_prob_t);
		//int c, o;

		char *buf = sbuf;
		if (buf[0] == '#') continue;
		while (isspace(*buf)) buf++;
		float gamma = strtof(buf, &buf);
		pb->gamma = gamma;
		while (isspace(*buf)) buf++;
		str2pattern(buf, &pb->p);
		assert(pb->p.n == 1);				/* One gamma per feature, please ! */

		uint32_t spi = feature2spatial(&pb->p.f[0]);
		assert(spi <= spat_dict->nspatials);		/* Bad patterns.spat / patterns.prob ? */
		if (feature_has_gamma(&pb->p.f[0]))
			die("%s: multiple gammas for feature %s\n", filename, pattern2sstr(&pb->p));
		pb->next = prob_dict->table[spi];
		prob_dict->table[spi] = pb;

		i++;
	}

	fclose(f);
	if (DEBUGL(1))  fprintf(stderr, "Loaded %d gammas.\n", i);
}

void
prob_dict_done()
{
	if (!prob_dict)  return;

	for (unsigned int id = 0; id < spat_dict->nspatials; id++)
		free(prob_dict->table[id]);
	free(prob_dict->table);
	free(prob_dict);
	prob_dict = NULL;
}

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

/* Do we have a gamma for that feature ? */
bool
feature_has_gamma(feature_t *f)
{
	uint32_t spi = feature2spatial(f);
	for (pattern_prob_t *pb = prob_dict->table[spi]; pb; pb = pb->next)
		if (feature_eq(f, &pb->p.f[0]))
			return true;
	return false;
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
