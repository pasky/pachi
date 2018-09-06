#ifndef PACHI_PATTERNPROB_H
#define PACHI_PATTERNPROB_H

/* Pattern probability table. */

#include <math.h>

#include "board.h"
#include "move.h"
#include "pattern.h"

/* The patterns probability dictionary */
extern struct prob_dict *prob_dict;

/* The pattern probability table considers each pattern as a whole
 * (not dividing it to individual features) and stores probability
 * of the pattern being played. */

/* The table primary key is the pattern spatial (most distinctive
 * feature); within a single primary key chain, the entries are
 * unsorted (for now). */

struct pattern_prob {
	struct pattern p;
	floating_t gamma;
	struct pattern_prob *next;
};

struct prob_dict {
	struct pattern_prob **table; /* [pc->spat_dict->nspatials + 1] */
};


/* Initialize the prob_dict data structure from a given file (pass NULL
 * to use default filename). */
void prob_dict_init(char *filename, struct pattern_config *pc);

/* Return probability associated with given pattern. */
static inline floating_t pattern_gamma(struct pattern_config *pc, struct pattern *p);

/* Evaluate patterns for all available moves. Stores found patterns to pats[b->flen]
 * and NON-normalized probability of each pattern to probs[b->flen].
 * Returns the sum of all probabilities that can be used for normalization. */
floating_t pattern_rate_moves(struct pattern_config *pc,
			      struct board *b, enum stone color,
			      struct pattern *pats, floating_t *probs,
			      struct ownermap *ownermap);

/* Helper function for pattern_match() callers:
 * Returns @locally flag to use for this position. */
bool pattern_matching_locally(struct pattern_config *pc,
			      struct board *b, enum stone color,				
			      struct ownermap *ownermap);

void print_pattern_best_moves(struct board *b, coord_t *best_c, float *best_r, int nbest);
void find_pattern_best_moves(struct board *b, float *probs, coord_t *best_c, float *best_r, int nbest);

/* Debugging */
void dump_gammas(strbuf_t *buf, struct pattern_config *pc, struct pattern *p);

/* Do we have a gamma for that feature ? */
bool feature_has_gamma(struct pattern_config *pc, struct feature *f);

/* Lookup gamma for that feature. */
static floating_t feature_gamma(struct pattern_config *pc, struct feature *f);

/* Compute pattern gamma */
static floating_t pattern_gamma(struct pattern_config *pc, struct pattern *p);

/* Extract spatial id from pattern feature.
 * If not a spatial feature returns highest spatial id plus one. */
static uint32_t feature2spatial(struct pattern_config *pc, struct feature *f);


static inline floating_t
feature_gamma(struct pattern_config *pc, struct feature *f)
{
	uint32_t spi = feature2spatial(pc, f);
	for (struct pattern_prob *pb = prob_dict->table[spi]; pb; pb = pb->next)
		if (feature_eq(f, &pb->p.f[0]))
			return pb->gamma;
	die("no gamma for feature (%s) !\n", feature2sstr(f));
	//return NAN; // XXX: We assume quiet NAN existence
}

static inline floating_t
pattern_gamma(struct pattern_config *pc, struct pattern *p)
{
	floating_t gammas = 1;
	for (int i = 0; i < p->n; i++)
		gammas *= feature_gamma(pc, &p->f[i]);
	return gammas;
}


static inline uint32_t
feature2spatial(struct pattern_config *pc, struct feature *f)
{
	if (f->id >= FEAT_SPATIAL3)
		return f->payload;
	return spat_dict->nspatials;
}


#endif
