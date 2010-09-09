#ifndef ZZGO_MQ_H
#define ZZGO_MQ_H

/* Move queues; in fact, they are more like move lists, usually used
 * to accumulate equally good move candidates, then choosing from them
 * randomly. But they are also used to juggle group lists (using the
 * fact that coord_t == group_t). */

#include "move.h"
#include "random.h"

#define MQL 512 /* XXX: On larger board this might not be enough. */
struct move_queue {
	unsigned int moves;
	coord_t move[MQL];
	/* Each move can have an optional tag or set of tags.
	 * The usage of these is user-dependent. */
	unsigned char tag[MQL];
};

/* Pick a random move from the queue. */
static coord_t mq_pick(struct move_queue *q);

/* Add a move to the queue. */
static void mq_add(struct move_queue *q, coord_t c, unsigned char tag);

/* Cat two queues together. */
static void mq_append(struct move_queue *qd, struct move_queue *qs);

/* Check if the last move in queue is not a dupe, and remove it
 * in that case. */
static void mq_nodup(struct move_queue *q);

/* Print queue contents on stderr. */
static void mq_print(struct move_queue *q, struct board *b, char *label);


static inline coord_t
mq_pick(struct move_queue *q)
{
	return q->moves ? q->move[fast_random(q->moves)] : pass;
}

static inline void
mq_add(struct move_queue *q, coord_t c, unsigned char tag)
{
	assert(q->moves < MQL);
	q->tag[q->moves] = tag;
	q->move[q->moves++] = c;
}

static inline void
mq_append(struct move_queue *qd, struct move_queue *qs)
{
	assert(qd->moves + qs->moves < MQL);
	memcpy(&qd->tag[qd->moves], qs->tag, qs->moves * sizeof(*qs->tag));
	memcpy(&qd->move[qd->moves], qs->move, qs->moves * sizeof(*qs->move));
	qd->moves += qs->moves;
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
mq_print(struct move_queue *q, struct board *b, char *label)
{
	fprintf(stderr, "%s candidate moves: ", label);
	for (unsigned int i = 0; i < q->moves; i++) {
		fprintf(stderr, "%s ", coord2sstr(q->move[i], b));
	}
	fprintf(stderr, "\n");
}

#endif
