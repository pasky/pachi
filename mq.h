#ifndef PACHI_MQ_H
#define PACHI_MQ_H

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


/* Variations of the above that allow move weighting. */
/* XXX: The "kinds of move queue" issue (it's even worse in some other
 * branches) is one of the few good arguments for C++ in Pachi...
 * At least rewrite it to be less hacky and maybe make a move_gamma_queue
 * that encapsulates move_queue. */

static coord_t mq_gamma_pick(struct move_queue *q, fixp_t *gammas);
static void mq_gamma_add(struct move_queue *q, fixp_t *gammas, coord_t c, double gamma, unsigned char tag);
static void mq_gamma_print(struct move_queue *q, fixp_t *gammas, struct board *b, char *label);


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
	for (unsigned int i = 1; i < 4; i++) {
		if (q->moves <= i)
			return;
		if (q->move[q->moves - 1 - i] == q->move[q->moves - 1]) {
			q->tag[q->moves - 1 - i] |= q->tag[q->moves - 1];
			q->moves--;
			return;
		}
	}
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

static inline coord_t
mq_gamma_pick(struct move_queue *q, fixp_t *gammas)
{
	if (!q->moves)
		return pass;
	fixp_t total = 0;
	for (unsigned int i = 0; i < q->moves; i++) {
		total += gammas[i];
	}
	if (!total)
		return pass;
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
mq_gamma_add(struct move_queue *q, fixp_t *gammas, coord_t c, double gamma, unsigned char tag)
{
	mq_add(q, c, tag);
	gammas[q->moves - 1] = double_to_fixp(gamma);
}

static inline void
mq_gamma_print(struct move_queue *q, fixp_t *gammas, struct board *b, char *label)
{
	fprintf(stderr, "%s candidate moves: ", label);
	for (unsigned int i = 0; i < q->moves; i++) {
		fprintf(stderr, "%s(%.3f) ", coord2sstr(q->move[i], b), fixp_to_double(gammas[i]));
	}
	fprintf(stderr, "\n");
}

#endif
