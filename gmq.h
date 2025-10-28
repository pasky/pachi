#ifndef PACHI_GMQ_H
#define PACHI_GMQ_H

/* Gamma Move queue:
 * Move queue that allows move weighting */

#include <assert.h>
#include "mq.h"
#include "fixp.h"
#include "move.h"
#include "random.h"

/* Gamma move queue */
typedef struct {
	int moves;
	coord_t move[MQL];
	fixp_t  gamma[MQL];
} gmq_t;


static void gmq_init(gmq_t *q);

/* Pick a random move from the queue. */
static coord_t gmq_pick(gmq_t *q);

/* Add a move to the queue (no dupe check). */
static void gmq_add(gmq_t *q, coord_t c, fixp_t gamma);

/* Print queue contents on stderr. */
static void gmq_print(gmq_t *q, char *label);
static void gmq_print_line(gmq_t *q, char *label);


static inline void
gmq_init(gmq_t *q)
{
	q->moves = 0;
}

static inline coord_t
gmq_pick(gmq_t *q)
{
	if (!q->moves)  return pass;

	fixp_t total = 0;
	for (int i = 0; i < q->moves; i++)
		total += q->gamma[i];
	if (!total)     return pass;

	fixp_t stab = fast_irandom(total);
	for (int i = 0; i < q->moves; i++) {
		if (stab < q->gamma[i])
			return q->move[i];
		stab -= q->gamma[i];
	}
	assert(0);
	return pass;
}

static inline void
gmq_add(gmq_t *q, coord_t c, fixp_t gamma)
{
	assert(q->moves < MQL);
	q->move[q->moves] = c;
	q->gamma[q->moves++] = gamma;
}

static inline void
gmq_print(gmq_t *q, char *label)
{
	fprintf(stderr, "%s", label);
	for (int i = 0; i < q->moves; i++)
		fprintf(stderr, "%s(%.3f) ", coord2sstr(q->move[i]), fixp_to_double(q->gamma[i]));
	fprintf(stderr, "\n");
}

static inline void
gmq_print_line(gmq_t *q, char *label)
{
	gmq_print(q, label);
	fprintf(stderr, "\n");
}


#endif
