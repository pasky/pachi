#ifndef PACHI_MQ_H
#define PACHI_MQ_H

/* Move queues; in fact, they are more like move lists, usually used
 * to accumulate equally good move candidates, then choosing from them
 * randomly. But they are also used to juggle group lists (using the
 * fact that coord_t == group_t). */

#include <assert.h>
#include "fixp.h"
#include "move.h"
#include "random.h"

#define MQL 512 /* XXX: On larger board this might not be enough. */
typedef struct {
	unsigned int moves;
	coord_t move[MQL];
	/* Each move can have an optional tag or set of tags.
	 * The usage of these is user-dependent. */
	unsigned char tag[MQL];
} move_queue_t;

static void mq_init(move_queue_t *q);

/* Pick a random move from the queue. */
static coord_t mq_pick(move_queue_t *q);

/* Add a move to the queue. */
static void mq_add(move_queue_t *q, coord_t c, unsigned char tag);

/* Is move in the queue ? */
static bool mq_has(move_queue_t *q, coord_t c);

/* Cat two queues together. */
static void mq_append(move_queue_t *qd, move_queue_t *qs);

/* Subtract two queues (find elements in a not in b) */
static void mq_sub(move_queue_t *a, move_queue_t *b, move_queue_t *res);

/* Check if the last move in queue is not a dupe, and remove it
 * in that case. */
static void mq_nodup(move_queue_t *q);

/* Print queue contents on stderr. */
static int  mq_print(char *label, move_queue_t *q);
static void mq_print_line(char *label, move_queue_t *q);


/* Variations of the above that allow move weighting. */
/* XXX: The "kinds of move queue" issue (it's even worse in some other
 * branches) is one of the few good arguments for C++ in Pachi...
 * At least rewrite it to be less hacky and maybe make a move_gamma_queue
 * that encapsulates move_queue. */

static coord_t mq_gamma_pick(move_queue_t *q, fixp_t *gammas);
static void mq_gamma_add(move_queue_t *q, fixp_t *gammas, coord_t c, fixp_t gamma, unsigned char tag);
static void mq_gamma_print(move_queue_t *q, fixp_t *gammas, char *label);


static inline void
mq_init(move_queue_t *q)
{
	q->moves = 0;
}

static inline coord_t
mq_pick(move_queue_t *q)
{
	return q->moves ? q->move[fast_random(q->moves)] : pass;
}

static inline void
mq_add(move_queue_t *q, coord_t c, unsigned char tag)
{
	assert(q->moves < MQL);
	q->tag[q->moves] = tag;
	q->move[q->moves++] = c;
}

static inline bool
mq_has(move_queue_t *q, coord_t c)
{
	for (unsigned int i = 0; i < q->moves; i++)
		if (q->move[i] == c)
			return true;
	return false;
}

static inline void
mq_append(move_queue_t *qd, move_queue_t *qs)
{
	assert(qd->moves + qs->moves < MQL);
	memcpy(&qd->tag[qd->moves], qs->tag, qs->moves * sizeof(*qs->tag));
	memcpy(&qd->move[qd->moves], qs->move, qs->moves * sizeof(*qs->move));
	qd->moves += qs->moves;
}

static inline void
mq_sub(move_queue_t *a, move_queue_t *b, move_queue_t *res)
{
	unsigned int n = a->moves;
	for (unsigned int i = 0; i < n; i++)
		if (!mq_has(b, a->move[i]))
			mq_add(res, a->move[i], 0);
}

static inline void
mq_nodup(move_queue_t *q)
{
	unsigned int n = q->moves;
	for (unsigned int i = 0; i < n - 1; i++) {
		if (q->move[i] == q->move[n - 1]) {
			q->tag[i] |= q->tag[n - 1];
			q->moves--;
			return;
		}
	}
}

static inline int
mq_print(char *label, move_queue_t *q)
{
	int n = fprintf(stderr, "%s", label);
	for (unsigned int i = 0; i < q->moves; i++)
		n += fprintf(stderr, "%s ", coord2sstr(q->move[i]));
	return n;
}

static inline void
mq_print_line(char *label, move_queue_t *q)
{
	mq_print(label, q);
	fprintf(stderr, "\n");
}

static inline coord_t
mq_gamma_pick(move_queue_t *q, fixp_t *gammas)
{
	if (!q->moves)  return pass;

	fixp_t total = 0;
	for (unsigned int i = 0; i < q->moves; i++)
		total += gammas[i];
	if (!total)     return pass;

	fixp_t stab = fast_irandom(total);
	for (unsigned int i = 0; i < q->moves; i++) {
		if (stab < gammas[i])
			return q->move[i];
		stab -= gammas[i];
	}
	assert(0);
	return pass;
}

static inline void
mq_gamma_add(move_queue_t *q, fixp_t *gammas, coord_t c, fixp_t gamma, unsigned char tag)
{
	mq_add(q, c, tag);
	gammas[q->moves - 1] = gamma;
}

static inline void
mq_gamma_print(move_queue_t *q, fixp_t *gammas, char *label)
{
	fprintf(stderr, "%s candidate moves: ", label);
	for (unsigned int i = 0; i < q->moves; i++)
		fprintf(stderr, "%s(%.3f) ", coord2sstr(q->move[i]), fixp_to_double(gammas[i]));
	fprintf(stderr, "\n");
}

#endif
