#ifndef PACHI_PATTERNPROB_H
#define PACHI_PATTERNPROB_H

#include <math.h>

#include "board.h"
#include "move.h"
#include "pattern/pattern.h"

/* Find pattern probability by dividing it into individual features
 * and using Rémi MM formula. The probability table has one gamma for
 * each possible feature. */

typedef struct {
	floating_t *gamma_table;    /* [pattern_gammas()] */
	feature_t  *feature_table;  /* [pattern_gammas()] */
} prob_dict_t;

/* The patterns probability dictionary */
extern prob_dict_t *prob_dict;


/* Initialize the prob_dict data structure from a given file (pass NULL
 * to use default filename). */
void prob_dict_init(char *filename);

/* Free patterns probability dictionary. */
void prob_dict_done();

/* Get pattern probabilities for all possible moves.
 * Stores normalized probability of each pattern in probs[b->flen] */
void pattern_rate_moves(board_t *b, enum stone color, floating_t *probs, pattern_context_t *ct);
/* Also stores found patterns in pats[b->flen] */
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

/* Print pattern features' gamma details in @buf */
void dump_gammas(strbuf_t *buf, pattern_t *p);

/* Do we have a gamma for that feature ? */
bool feature_has_gamma(feature_t *f);


#endif
