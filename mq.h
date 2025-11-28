#ifndef PACHI_MQ_H
#define PACHI_MQ_H

/* Move queues; in fact, they are more like move lists, usually used
 * to accumulate equally good move candidates, then choosing from them
 * randomly. But they are also used to juggle group lists (using the
 * fact that coord_t == group_t). */

#include <assert.h>
#include "move.h"
#include "random.h"

#define MQL 512 /* XXX: On larger board this might not be enough. */

/* Move queue */
typedef struct {
	int moves;
	coord_t move[MQL];
} mq_t;


static void mq_init(mq_t *q);

/* Pick a random move from the queue. */
static coord_t mq_pick(mq_t *q);

/* Add a move to the queue (no dupe check). */
static void mq_add(mq_t *q, coord_t c);

/* Add a move to the queue (except if already in). */
#define mq_add_nodup(q, c)	do {  mq_add((q), (c));  mq_nodup(q);  } while(0)

/* Remove move from queue */
static void mq_remove(mq_t *q, coord_t c);

/* Remove i'th item from queue */
static void mq_remove_index(mq_t *q, int i);

/* Is move in the queue ? */
static bool mq_has(mq_t *q, coord_t c);

/* Cat two queues together. */
static void mq_append(mq_t *qd, mq_t *qs);

/* Subtract two queues (find elements in a not in b) */
static void mq_sub(mq_t *a, mq_t *b, mq_t *res);

/* Check if the last move in queue is not a dupe, and remove it
 * in that case. */
static void mq_nodup(mq_t *q);

/* Print queue contents. */
static int  mq_print_file(mq_t *q, FILE *f, char *label);
/* Print queue contents on stderr. */
static int  mq_print(mq_t *q, char *label);
static void mq_print_line(mq_t *q, char *label);



static inline void
mq_init(mq_t *q)
{
	q->moves = 0;
}

static inline coord_t
mq_pick(mq_t *q)
{
	return q->moves ? q->move[fast_random(q->moves)] : pass;
}

static inline void
mq_add(mq_t *q, coord_t c)
{
	assert(q->moves < MQL);
	q->move[q->moves++] = c;
}

static inline void
mq_remove(mq_t *q, coord_t c)
{
	for (int i = 0; i < q->moves; i++)
		if (q->move[i] == c)
			mq_remove_index(q, i--);
}

static inline void
mq_remove_index(mq_t *q, int i)
{
	assert(q->moves);
	q->move[i] = q->move[q->moves-- - 1];
}

static inline bool
mq_has(mq_t *q, coord_t c)
{
	for (int i = 0; i < q->moves; i++)
		if (q->move[i] == c)
			return true;
	return false;
}

static inline void
mq_append(mq_t *qd, mq_t *qs)
{
	assert(qd->moves + qs->moves < MQL);
	memcpy(&qd->move[qd->moves], qs->move, qs->moves * sizeof(*qs->move));
	qd->moves += qs->moves;
}

static inline void
mq_sub(mq_t *a, mq_t *b, mq_t *res)
{
	int n = a->moves;
	for (int i = 0; i < n; i++)
		if (!mq_has(b, a->move[i]))
			mq_add(res, a->move[i]);
}

static inline void
mq_nodup(mq_t *q)
{
	int n = q->moves;
	for (int i = 0; i < n - 1; i++) {
		if (q->move[i] == q->move[n - 1]) {
			q->moves--;
			return;
		}
	}
}

static inline int
mq_print_file(mq_t *q, FILE *f, char *label)
{
	int n = fprintf(f, "%s", label);
	for (int i = 0; i < q->moves; i++)
		n += fprintf(f, "%s ", coord2sstr(q->move[i]));
	return n;
}

static inline int
mq_print(mq_t *q, char *label)
{
	return mq_print_file(q, stderr, label);
}

static inline void
mq_print_line(mq_t *q, char *label)
{
	mq_print(q, label);
	fprintf(stderr, "\n");
}


#endif
