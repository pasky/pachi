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

/* Evaluate patterns for all available moves. Stores found patterns to pats[b->flen]
 * and NON-normalized probability of each pattern to probs[b->flen].
 * Returns the sum of all probabilities that can be used for normalization. */
void pattern_rate_moves(board_t *b, enum stone color, floating_t *probs, pattern_context_t *ct);
/* Save pattern for each move as well. */
void pattern_rate_moves_full(board_t *b, enum stone color,
			     pattern_t *pats, floating_t *probs,
			     pattern_context_t *ct);
/* For testing purposes: no prioritized features, check every feature. */
void pattern_rate_moves_vanilla(board_t *b, enum stone color,
				pattern_t *pats, floating_t *probs,
				pattern_context_t *ct);


/* Helper function for pattern_match() callers:
 * Returns @locally flag to use for this position. */
bool pattern_matching_locally(board_t *b, enum stone color, pattern_context_t *ct);

void print_pattern_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest);
void get_pattern_best_moves(board_t *b, floating_t *probs, coord_t *best_c, float *best_r, int nbest);

/* Debugging */
void dump_gammas(strbuf_t *buf, pattern_t *p, pattern_config_t *pc);

/* Do we have a gamma for that feature ? */
bool feature_has_gamma(feature_t *f, pattern_config_t *pc);

/* Lookup gamma for that feature. */
static floating_t feature_gamma(feature_t *f, pattern_config_t *pc);

/* Return probability associated with given pattern. */
static floating_t pattern_gamma(pattern_t *p, pattern_config_t *pc);

/* Extract spatial id from pattern feature.
 * If not a spatial feature returns highest spatial id plus one. */
static uint32_t feature2spatial(feature_t *f, pattern_config_t *pc);


static inline floating_t
feature_gamma(feature_t *f, pattern_config_t *pc)
{
	uint32_t spi = feature2spatial(f, pc);
	for (pattern_prob_t *pb = prob_dict->table[spi]; pb; pb = pb->next)
		if (feature_eq(f, &pb->p.f[0]))
			return pb->gamma;
	die("no gamma for feature (%s) !\n", feature2sstr(f));
	//return NAN; // XXX: We assume quiet NAN existence
}

static inline floating_t
pattern_gamma(pattern_t *p, pattern_config_t *pc)
{
	floating_t gammas = 1;
	for (int i = 0; i < p->n; i++)
		gammas *= feature_gamma(&p->f[i], pc);
	return gammas;
}


static inline uint32_t
feature2spatial(feature_t *f, pattern_config_t *pc)
{
	if (f->id >= FEAT_SPATIAL3)
		return f->payload;
	return spat_dict->nspatials;
}


#endif
