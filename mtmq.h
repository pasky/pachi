#ifndef PACHI_MTMQ_H
#define PACHI_MTMQ_H

/* Multiple tags move queues:
 * Move queue where each move can have an optional set of tags.
 * There is a maximum of 32 tags as each tag should set one bit only
 * (q->tag[i] works as a bitfield). When duplicate move entries are removed
 * tags are merged together. */

#include <assert.h>
#include "mq.h"
#include "move.h"
#include "random.h"

/* Tagged move queue */
typedef struct {
	int moves;
	coord_t move[MQL];
	int     tag[MQL];    	/* Each move can have up to 32 tags (bitfield) */
} mtmq_t;

static void mtmq_init(mtmq_t *q);

/* Add a move to the queue (no dupe check). */
static void mtmq_add(mtmq_t *q, coord_t c, int tag);

/* Add a move to the queue (except if already in). */
#define mtmq_add_nodup(q, c, tag)	do {  mtmq_add((q), (c), (tag));  mtmq_nodup(q);  } while(0)

/* Remove last move in the queue if it's a dupe. 
 * Preserve its tag though (merge with other move) */
static void mtmq_nodup(mtmq_t *q);

/* Print queue contents on stderr. */
static int  mtmq_print(mtmq_t *q, char *label);
static void mtmq_print_line(mtmq_t *q, char *label);



static inline void
mtmq_init(mtmq_t *q)
{
	q->moves = 0;
}

static inline void
mtmq_add(mtmq_t *q, coord_t c, int tag)
{
	assert(q->moves < MQL);
	q->tag[q->moves] = tag;
	q->move[q->moves++] = c;
}

static inline void
mtmq_nodup(mtmq_t *q)
{
	int n = q->moves;
	for (int i = 0; i < n - 1; i++) {
		if (q->move[i] == q->move[n - 1]) {
			/* Merge both moves' tags */
			q->tag[i] |= q->tag[n - 1];
			q->moves--;
			return;
		}
	}
}

static inline int
mtmq_print(mtmq_t *q, char *label)
{
	int n = fprintf(stderr, "%s", label);
	for (int i = 0; i < q->moves; i++)
		n += fprintf(stderr, "%s ", coord2sstr(q->move[i]));
	return n;
}

static inline void
mtmq_print_line(mtmq_t *q, char *label)
{
	mtmq_print(q, label);
	fprintf(stderr, "\n");
}


#endif
