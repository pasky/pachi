#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "patternsp.h"
#include "patternprob.h"
#include "engine.h"

prob_dict_t    *prob_dict = NULL;

void
prob_dict_init(char *filename, pattern_config_t *pc)
{
	assert(!prob_dict);
	if (!filename)  filename = "patterns_mm.gamma";
	FILE *f = fopen_data_file(filename, "r");
	if (!f) {
		if (DEBUGL(1))  fprintf(stderr, "%s not found, will not use mm patterns.\n", filename);
		return;
	}

	prob_dict = calloc2(1, sizeof(*prob_dict));
	prob_dict->table = calloc2(spat_dict->nspatials + 1, sizeof(*prob_dict->table));

	int i = 0;
	char sbuf[1024];
	while (fgets(sbuf, sizeof(sbuf), f)) {
		pattern_prob_t *pb = calloc2(1, sizeof(*pb));
		//int c, o;

		char *buf = sbuf;
		if (buf[0] == '#') continue;
		while (isspace(*buf)) buf++;
		float gamma = strtof(buf, &buf);
		pb->gamma = gamma;
		while (isspace(*buf)) buf++;
		str2pattern(buf, &pb->p);
		assert(pb->p.n == 1);				/* One gamma per feature, please ! */

		uint32_t spi = feature2spatial(pc, &pb->p.f[0]);
		assert(spi <= spat_dict->nspatials);		/* Bad patterns.spat / patterns.prob ? */
		if (feature_has_gamma(pc, &pb->p.f[0]))
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

static floating_t
rescale_probs(board_t *b, floating_t *probs, floating_t max)
{
	floating_t total = 0;
	
	for (int f = 0; f < b->flen; f++) {
		if (isnan(probs[f]))  continue;
		probs[f] /= max;
		total += probs[f];
	}
	
	//fprintf(stderr, "pattern probs total: %.2f\n", total);
	return total;
}

static floating_t
pattern_max_rating(pattern_config_t *pc,
		   board_t *b, enum stone color,
		   pattern_t *pats, floating_t *probs,
		   ownermap_t *ownermap,
		   bool locally)
{
	floating_t max = -10000000;
	for (int f = 0; f < b->flen; f++) {
		probs[f] = NAN;

		move_t mo = move(b->f[f], color);
		if (is_pass(mo.coord))	continue;
		if (!board_is_valid_play_no_suicide(b, mo.color, mo.coord)) continue;

		pattern_match(pc, &pats[f], b, &mo, ownermap, locally);
		floating_t prob = pattern_gamma(pc, &pats[f]);
		if (!isnan(prob)) {
			probs[f] = prob;
			if (prob > max)  max = prob;
		}
		if (DEBUGL(5)) {
			char buf[256]; pattern2str(buf, &pats[f]);
			fprintf(stderr, "=> move %s pattern %s prob %.3f\n", coord2sstr(mo.coord), buf, prob);
		}
	}

	return max;
}

#define LOW_PATTERN_RATING 6.0

floating_t
pattern_rate_moves(pattern_config_t *pc,
                   board_t *b, enum stone color,
                   pattern_t *pats, floating_t *probs,
		   ownermap_t *ownermap)
{
#ifdef PATTERN_FEATURE_STATS
	pattern_stats_new_position();
#endif

	/* Try local moves first. */
	floating_t max = pattern_max_rating(pc, b, color, pats, probs, ownermap, true);

	/* Nothing big matches ? Try again ignoring distance so we get good tenuki moves. */
	if (max < LOW_PATTERN_RATING)
		max = pattern_max_rating(pc, b, color, pats, probs, ownermap, false);
	
	return rescale_probs(b, probs, max);
}

bool
pattern_matching_locally(pattern_config_t *pc,
			 board_t *b, enum stone color,
			 ownermap_t *ownermap)
{
	pattern_t pats[b->flen];
	floating_t probs[b->flen];
	floating_t max = pattern_max_rating(pc, b, color, pats, probs, ownermap, true);
	return (max >= LOW_PATTERN_RATING);
}

void
dump_gammas(strbuf_t *buf, pattern_config_t *pc, pattern_t *p)
{
	char head[4] = { 0, };
	floating_t gamma = pattern_gamma(pc, p);
	sbprintf(buf, "%.2f = ", gamma);
	
	for (int i = 0; i < p->n; i++) {
		feature_t *f = &p->f[i];		
		sbprintf(buf, "%s(%s) %.2f ", head, feature2sstr(f), feature_gamma(pc, f));
		strcpy(head, "* ");
		continue;
	}
}

/* Do we have a gamma for that feature ? */
bool
feature_has_gamma(pattern_config_t *pc, feature_t *f)
{
	uint32_t spi = feature2spatial(pc, f);
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
get_pattern_best_moves(board_t *b, float *probs, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}
	
	for (int f = 0; f < b->flen; f++)
		if (!isnan(probs[f]))
			best_moves_add(b->f[f], probs[f], best_c, best_r, nbest);
}
