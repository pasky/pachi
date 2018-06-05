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


struct prob_dict*
prob_dict_init(char *filename, struct pattern_config *pc)
{
	if (!filename)  filename = "patterns_mm.gamma";
	FILE *f = fopen(filename, "r");
	if (!f) {
		if (DEBUGL(1))  fprintf(stderr, "%s not found, will not use mm patterns.\n", filename);
		return NULL;
	}

	struct prob_dict *dict = calloc2(1, sizeof(*dict));
	dict->table = calloc2(spat_dict->nspatials + 1, sizeof(*dict->table));

	char *sphcachehit = calloc2(spat_dict->nspatials, 1);
	hash_t (*sphcache)[PTH__ROTATIONS] = malloc(spat_dict->nspatials * sizeof(sphcache[0]));

	int i = 0;
	char sbuf[1024];
	while (fgets(sbuf, sizeof(sbuf), f)) {
		struct pattern_prob *pb = calloc2(1, sizeof(*pb));
		//int c, o;

		char *buf = sbuf;
		if (buf[0] == '#') continue;
		while (isspace(*buf)) buf++;
		float gamma = strtof(buf, &buf);
		pb->gamma = gamma;
		while (isspace(*buf)) buf++;
		str2pattern(buf, &pb->p);
		assert(pb->p.n == 1);  /* One gamma per feature, please ! */

		uint32_t spi = pattern2spatial(pc, &pb->p);
		assert(spi <= spat_dict->nspatials); /* Bad patterns.spat / patterns.prob ? */
		pb->next = dict->table[spi];
		dict->table[spi] = pb;

		/* Some spatials may not have been loaded if they correspond
		 * to a radius larger than supported. */
		if (spat_dict->spatials[spi].dist > 0) {
			/* We rehash spatials in the order of loaded patterns. This way
			 * we make sure that the most popular patterns will be hashed
			 * last and therefore take priority. */
			// FIXME reorder gammas ?
			if (!sphcachehit[spi]) {
				sphcachehit[spi] = 1;
				for (unsigned int r = 0; r < PTH__ROTATIONS; r++)
					sphcache[spi][r] = spatial_hash(r, &spat_dict->spatials[spi]);
			}
			for (unsigned int r = 0; r < PTH__ROTATIONS; r++)
				spatial_dict_addh(spat_dict, sphcache[spi][r], spi);
		}

		i++;
	}

	free(sphcache);
	free(sphcachehit);
	if (DEBUGL(3))  spatial_dict_hashstats(spat_dict);

	fclose(f);
	if (DEBUGL(1))  fprintf(stderr, "Loaded %d gammas.\n", i);
	
	return dict;
}

static floating_t
rescale_probs(struct board *b, floating_t *probs, floating_t max)
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
pattern_max_rating(struct pattern_config *pc,
		   struct board *b, enum stone color,
		   struct pattern *pats, floating_t *probs,
		   struct ownermap *ownermap,
		   bool locally)
{
	//assert(ownermap->playouts >= GJ_MINGAMES);
	assert(ownermap->playouts >= 100);
	
	floating_t max = -10000000;
	for (int f = 0; f < b->flen; f++) {
		probs[f] = NAN;

		struct move mo = { .coord = b->f[f], .color = color };
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
			fprintf(stderr, "=> move %s pattern %s prob %.3f\n", coord2sstr(mo.coord, b), buf, prob);
		}
	}

	return max;
}

#define LOW_PATTERN_RATING 6.0

floating_t
pattern_rate_moves(struct pattern_config *pc,
                   struct board *b, enum stone color,
                   struct pattern *pats, floating_t *probs,
		   struct ownermap *ownermap)
{
#ifdef PATTERN_FEATURE_STATS
	pattern_stats_new_position();
#endif

	/* Try local moves first. */
	floating_t max = pattern_max_rating(pc, b, color, pats, probs, ownermap, true);

	/* Nothing big matches ? Try again ignoring distance so we get good tenuki moves. */
	if (max < LOW_PATTERN_RATING)
		max = pattern_max_rating(pc, b, color, pats, probs, ownermap, false);
	
	floating_t total = rescale_probs(b, probs, max);
	return total;
}

bool
pattern_matching_locally(struct pattern_config *pc,
			 struct board *b, enum stone color,
			 struct ownermap *ownermap)
{
	struct pattern pats[b->flen];
	floating_t probs[b->flen];
	floating_t max = pattern_max_rating(pc, b, color, pats, probs, ownermap, true);
	return (max >= LOW_PATTERN_RATING);
}

void
dump_gammas(strbuf_t *buf, struct pattern_config *pc, struct pattern *p)
{
	char head[4] = { 0, };
	floating_t gamma = pattern_gamma(pc, p);
	sbprintf(buf, "%.2f = ", gamma);
	
	for (int i = 0; i < p->n; i++) {
		struct feature *f = &p->f[i];		
		sbprintf(buf, "%s(%s) %.2f ", head, feature2sstr(f), feature_gamma(pc, f));
		strcpy(head, "* ");
		continue;
	}
}

void
print_pattern_best_moves(struct board *b, coord_t *best_c, float *best_r, int nbest)
{
	int cols = fprintf(stderr, "patterns = [ ");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3s ", coord2sstr(best_c[i], b));
	fprintf(stderr, "]\n");

	fprintf(stderr, "%*s[ ", cols-2, "");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3i ", (int)(best_r[i] * 100));
	fprintf(stderr, "]\n");
}

void
find_pattern_best_moves(struct board *b, float *probs, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++)
		best_c[i] = pass;
	
	for (int f = 0; f < b->flen; f++)
		if (!isnan(probs[f]))
			best_moves_add(b->f[f], probs[f], best_c, best_r, nbest);
}
