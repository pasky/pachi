#ifndef PACHI_PATTERNPROB_H
#define PACHI_PATTERNPROB_H

/* Pattern probability table. */

#include <math.h>

#include "board.h"
#include "move.h"
#include "pattern/pattern.h"

/* The pattern probability table considers each pattern as a whole
 * (not dividing it to individual features) and stores probability
 * of the pattern being played. */

/* The table primary key is the pattern spatial (most distinctive
 * feature); within a single primary key chain, the entries are
 * unsorted (for now). */

typedef struct pattern_prob {
	pattern_t p;
	floating_t gamma;
	struct pattern_prob *next;
} pattern_prob_t;

typedef struct {
	pattern_prob_t **table; /* [pc->spat_dict->nspatials + 1] */
} prob_dict_t;

/* The patterns probability dictionary */
extern prob_dict_t *prob_dict;


/* Initialize the prob_dict data structure from a given file (pass NULL
 * to use default filename). */
void prob_dict_init(char *filename, pattern_config_t *pc);

/* Free patterns probability dictionary. */
void prob_dict_done();

/* Return probability associated with given pattern. */
static inline floating_t pattern_gamma(pattern_config_t *pc, pattern_t *p);

/* Evaluate patterns for all available moves. Stores found patterns to pats[b->flen]
 * and NON-normalized probability of each pattern to probs[b->flen].
 * Returns the sum of all probabilities that can be used for normalization. */
floating_t pattern_rate_moves_fast(pattern_config_t *pc,
				   board_t *b, enum stone color,
				   floating_t *probs,
				   ownermap_t *ownermap);
/* Save pattern for each move as well. */
floating_t pattern_rate_moves(pattern_config_t *pc,
			      board_t *b, enum stone color,
			      pattern_t *pats, floating_t *probs,
			      ownermap_t *ownermap);
/* For testing purposes: no prioritized features, check every feature. */
floating_t pattern_rate_moves_vanilla(pattern_config_t *pc,
				      board_t *b, enum stone color,
				      pattern_t *pats, floating_t *probs,
				      ownermap_t *ownermap);


/* Helper function for pattern_match() callers:
 * Returns @locally flag to use for this position. */
bool pattern_matching_locally(pattern_config_t *pc,
			      board_t *b, enum stone color,				
			      ownermap_t *ownermap);

void print_pattern_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest);
void get_pattern_best_moves(board_t *b, floating_t *probs, coord_t *best_c, float *best_r, int nbest);

/* Debugging */
void dump_gammas(strbuf_t *buf, pattern_config_t *pc, pattern_t *p);

/* Do we have a gamma for that feature ? */
bool feature_has_gamma(pattern_config_t *pc, feature_t *f);

/* Lookup gamma for that feature. */
static floating_t feature_gamma(pattern_config_t *pc, feature_t *f);

/* Compute pattern gamma */
static floating_t pattern_gamma(pattern_config_t *pc, pattern_t *p);

/* Extract spatial id from pattern feature.
 * If not a spatial feature returns highest spatial id plus one. */
static uint32_t feature2spatial(pattern_config_t *pc, feature_t *f);


static inline floating_t
feature_gamma(pattern_config_t *pc, feature_t *f)
{
	uint32_t spi = feature2spatial(pc, f);
	for (pattern_prob_t *pb = prob_dict->table[spi]; pb; pb = pb->next)
		if (feature_eq(f, &pb->p.f[0]))
			return pb->gamma;
	die("no gamma for feature (%s) !\n", feature2sstr(f));
	//return NAN; // XXX: We assume quiet NAN existence
}

static inline floating_t
pattern_gamma(pattern_config_t *pc, pattern_t *p)
{
	floating_t gammas = 1;
	for (int i = 0; i < p->n; i++)
		gammas *= feature_gamma(pc, &p->f[i]);
	return gammas;
}


static inline uint32_t
feature2spatial(pattern_config_t *pc, feature_t *f)
{
	if (f->id >= FEAT_SPATIAL3)
		return f->payload;
	return spat_dict->nspatials;
}


#endif
