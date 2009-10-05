#ifndef ZZGO_MQ_H
#define ZZGO_MQ_H

/* Move queues; in fact, they are more like move lists, used to accumulate
 * equally good move candidates, then choosing from them randomly. */

#include "move.h"
#include "random.h"

#define MQL 64
struct move_queue {
	int moves;
	coord_t move[MQL];
};

/* Pick a random move from the queue. */
static coord_t mq_pick(struct move_queue *q);

/* Add a move to the queue. */
static void mq_add(struct move_queue *q, coord_t c);

/* Cat two queues together. */
static void mq_append(struct move_queue *qd, struct move_queue *qs);

/* Check if the last move in queue is not a dupe, and remove it
 * in that case. */
static void mq_nodup(struct move_queue *q);


static inline coord_t
mq_pick(struct move_queue *q)
{
	return q->moves ? q->move[fast_random(q->moves)] : pass;
}

static inline void
mq_nodup(struct move_queue *q)
{
	if ((q->moves > 1 && q->move[q->moves - 2] == q->move[q->moves - 1])
	    || (q->moves > 2 && q->move[q->moves - 3] == q->move[q->moves - 1])
	    || (q->moves > 3 && q->move[q->moves - 4] == q->move[q->moves - 1]))
		q->moves--;
}

static inline void
mq_append(struct move_queue *qd, struct move_queue *qs)
{
	assert(qd->moves + qs->moves < MQL);
	memcpy(&qd->move[qd->moves], qs->move, qs->moves * sizeof(*qs->move));
	qd->moves += qs->moves;
}

static inline void
mq_add(struct move_queue *q, coord_t c)
{
	assert(q->moves < MQL);
	q->move[q->moves++] = c;
}

#endif
