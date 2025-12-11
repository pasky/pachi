#ifndef PACHI_PATTERNPROB_H
#define PACHI_PATTERNPROB_H

#include <math.h>

#include "board.h"
#include "move.h"
#include "engine.h"
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
 * Store normalized probability of each pattern in probs[b->flen].
 * @pats            (optional): save pattern for each move in pats[b->flen].
 * @matched_locally (optional): store whether local match was used or distance features were ignored. */
void pattern_rate_moves(board_t *b, enum stone color, floating_t *probs, pattern_t *pats, pattern_context_t *ct, bool *matched_locally);
/* For testing purposes: no prioritized features, check every feature. */
void pattern_rate_moves_vanilla(board_t *b, enum stone color, floating_t *probs, pattern_t *pats, pattern_context_t *ct);

/* Helper function for pattern_match() callers: returns @locally flag to use for this position.
 * For gogui only (super inefficient). */
bool pattern_matching_locally(board_t *b, enum stone color, pattern_context_t *ct);

void print_pattern_best_moves(best_moves_t *best);
void get_pattern_best_moves(board_t *b, floating_t *probs, best_moves_t *best);

/* Print pattern features' gamma details in @buf */
void dump_gammas(strbuf_t *buf, pattern_t *p);

/* Do we have a gamma for that feature ? */
bool feature_has_gamma(feature_t *f);


#endif
