#ifndef PACHI_PATTERNPROB_H
#define PACHI_PATTERNPROB_H

/* Pattern probability table. */

#include <math.h>

#include "board.h"
#include "move.h"
#include "pattern.h"


/* The pattern probability table considers each pattern as a whole
 * (not dividing it to individual features) and stores probability
 * of the pattern being played. */

/* The table primary key is the pattern spatial (most distinctive
 * feature); within a single primary key chain, the entries are
 * unsorted (for now). */

struct pattern_prob {
	struct pattern p;
	floating_t prob;
	struct pattern_prob *next;
};

struct pattern_pdict {
	struct pattern_config *pc;

	struct pattern_prob **table; /* [pc->spat_dict->nspatials + 1] */
};

/* Initialize the pdict data structure from a given file (pass NULL
 * to use default filename). Returns NULL if the file with patterns
 * has been found. */
struct pattern_pdict *pattern_pdict_init(char *filename, struct pattern_config *pc);

/* Return probability associated with given pattern. Returns NaN if
 * the pattern cannot be found. */
static floating_t pattern_prob(struct pattern_pdict *dict, struct pattern *p);

/* Evaluate patterns for all available moves. Stores found patterns
 * to pats[b->flen] and NON-normalized probability of each pattern
 * (or NaN in case of no match) to probs[b->flen]. Returns the sum
 * of all probabilities that can be used for normalization. */
floating_t pattern_rate_moves(struct pattern_setup *pat,
                        struct board *b, enum stone color,
                        struct pattern *pats, floating_t *probs);

/* Utility function - extract spatial id from a pattern. If the pattern
 * has no spatial feature, it is represented by the highest spatial id
 * plus one. */
static uint32_t pattern2spatial(struct pattern_pdict *dict, struct pattern *p);


static inline floating_t
pattern_prob(struct pattern_pdict *dict, struct pattern *p)
{
	uint32_t spi = pattern2spatial(dict, p);
	for (struct pattern_prob *pb = dict->table[spi]; pb; pb = pb->next)
		if (pattern_eq(p, &pb->p))
			return pb->prob;
	return NAN; // XXX: We assume quiet NAN existence
}

static inline uint32_t
pattern2spatial(struct pattern_pdict *dict, struct pattern *p)
{
	for (int i = 0; i < p->n; i++)
		if (p->f[i].id == FEAT_SPATIAL)
			return p->f[i].payload;
	return dict->pc->spat_dict->nspatials;
}

#endif
