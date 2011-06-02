#ifndef PACHI_LIBMAP_H
#define PACHI_LIBMAP_H

/* "Liberty map" - description of a particular liberty structure of a group.
 * The idea is that we can track local tactical effectivity of various moves
 * within the particular liberty structure context. */

#include "board.h"
#include "mq.h"
#include "stats.h"

hash_t group_to_libmap(struct board *b, group_t group);


/* Our own version of move_queue, but including liberty maps of moves. */
/* The user will usually first create a queue of tactical goals and pick
 * (using libmap_mq functions below), then add that one to libmap_hash's
 * global move queue, processed at the end of the whole playout. */

struct libmap_group {
	/* Group-relative tactical description of a move. */
	group_t group;
	hash_t hash;
	enum stone goal;
};

struct libmap_mq {
	struct move_queue mq;
	enum stone color[MQL]; // complements mq.move
	struct libmap_group group[MQL];
};

/* libmap_mq_pick() would be simple fast_random(mq.moves), but c.f.
 * libmap_queue_mqpick() below. */
static void libmap_mq_add(struct libmap_mq *q, struct move m, unsigned char tag, struct libmap_group group);
static void libmap_mq_nodup(struct libmap_mq *q);
static void libmap_mq_print(struct libmap_mq *q, struct board *b, char *label);

/* Is the same move that is in queue with differing group information
 * supposed to stay multiple times in the queue? */
#define LIBMAP_MQ_GROUP_EXCL


/* Tactical application - hash structure storing info about move effectivity. */

struct libmap_move {
	struct move move;
	struct move_stats stats;
};

struct libmap_context {
	hash_t hash;
	/* We add moves in multiple threads. But at most, on conflict we will
	 * end up with tiny amount of misappropriated playouts. */
	int moves;
	struct libmap_move move[GROUP_REFILL_LIBS];
};

struct libmap_hash {
	struct board *b;
	int refcount;

	/* Queue of moves to store at the game end. */
	struct libmap_mq queue;

	/* Stored statistics. */
	/* We store statistics in a hash table without separated chains;
	 * if bucket is occupied, we look into the following ones,
	 * allowing up to libmap_hash_maxline subsequent checks. */
	/* XXX: We mishandle hashes >= UINT64_MAX - libmap_hash_maxline. */
#define libmap_hash_bits 19
#define libmap_hash_size (1 << libmap_hash_bits)
#define libmap_hash_mask (libmap_hash_size - 1)
#define libmap_hash_maxline 32
	struct libmap_context hash[libmap_hash_size];
};

/* Get a new libmap. */
struct libmap_hash *libmap_init(struct board *b);
/* Release libmap. Based on refcount, this will free it. */
void libmap_put(struct libmap_hash *lm);

/* Pick a move from @q, enqueue it in lm.queue and return its coordinate. */
static coord_t libmap_queue_mqpick(struct libmap_hash *lm, struct libmap_mq *q);
/* Record queued moves in the hashtable based on final position of b. */
void libmap_queue_process(struct libmap_hash *lm, struct board *b);
/* Add a result to the hashed statistics. */
void libmap_add_result(struct libmap_hash *lm, hash_t hash, struct move move, floating_t result, int playouts);
/* Get statistics of particular move in given libmap structure. */
static struct move_stats *libmap_move_stats(struct libmap_hash *lm, hash_t hash, struct move move);



static inline void
libmap_mq_add(struct libmap_mq *q, struct move m, unsigned char tag, struct libmap_group group)
{
	assert(q->mq.moves < MQL);
	q->mq.tag[q->mq.moves] = tag;
	q->mq.move[q->mq.moves] = m.coord;
	q->color[q->mq.moves] = m.color;
	q->group[q->mq.moves] = group;
	q->mq.moves++;
}

static inline void
libmap_mq_nodup(struct libmap_mq *q)
{
	for (unsigned int i = 1; i < 4; i++) {
		if (q->mq.moves <= i)
			return;
		if (q->mq.move[q->mq.moves - 1 - i] == q->mq.move[q->mq.moves - 1]
#ifdef LIBMAP_MQ_GROUP_EXCL
		    && q->group[q->mq.moves - 1 - i].group == q->group[q->mq.moves - 1].group
		    && q->group[q->mq.moves - 1 - i].hash == q->group[q->mq.moves - 1].hash
		    && q->group[q->mq.moves - 1 - i].goal == q->group[q->mq.moves - 1].goal
#endif
			) {
			q->mq.tag[q->mq.moves - 1 - i] |= q->mq.tag[q->mq.moves - 1];
			assert(q->color[q->mq.moves - 1 - i] == q->color[q->mq.moves - 1]);
			/* ifndef LIBMAP_MQ_GROUP_EXCL, original group info
			 * survives. */
			q->mq.moves--;
			return;
		}
	}
}

static inline void
libmap_mq_print(struct libmap_mq *q, struct board *b, char *label)
{
	fprintf(stderr, "%s candidate moves: ", label);
	for (unsigned int i = 0; i < q->mq.moves; i++) {
		fprintf(stderr, "%s[%c:%s]", coord2sstr(q->mq.move[i], b),
			/* attacker / defender */
			board_at(b, q->group[i].group) == q->group[i].goal ? 'd' : 'a',
			coord2sstr(q->group[i].group, b));
		struct move m = { .coord = q->mq.move[i], .color = q->color[i] };
		struct move_stats *ms = libmap_move_stats(b->libmap, q->group[i].hash, m);
		if (ms) {
			fprintf(stderr, "(%.3f/%d)", ms->value, ms->playouts);
		}
		fputc(' ', stderr);
	}
	fputc('\n', stderr);
}


static inline coord_t
libmap_queue_mqpick(struct libmap_hash *lm, struct libmap_mq *q)
{
	if (!q->mq.moves)
		return pass; // nothing to do
	unsigned int p = 0, pp;
	if (q->mq.moves == 1)
		goto pick;

	/* Pick random move, up to a simple check - if a move has tactical
	 * rating <0.5, prefer another. (FIXME: Up to epsilon?) */
	p = pp = fast_random(q->mq.moves);
	do {
		int pm = p % q->mq.moves;
		struct move m = { .coord = q->mq.move[pm], .color = q->color[pm] };
		struct move_stats *ms = libmap_move_stats(lm, q->group[pm].hash, m);
		if (!ms || ms->value >= 0.5) break;
	} while (++p % q->mq.moves < pp);
	p %= q->mq.moves;

pick:;
	struct move m = { .coord = q->mq.move[p], .color = q->color[p] };
	libmap_mq_add(&lm->queue, m, q->mq.tag[p], q->group[p]);

	return q->mq.move[p];
}


static inline struct move_stats *
libmap_move_stats(struct libmap_hash *lm, hash_t hash, struct move move)
{
	hash_t ih;
	for (ih = hash; lm->hash[ih & libmap_hash_mask].hash != hash; ih++) {
		if (lm->hash[ih & libmap_hash_mask].moves == 0)
			return NULL;
		if (ih >= hash + libmap_hash_maxline)
			return NULL;
	}
	struct libmap_context *lc = &lm->hash[ih & libmap_hash_mask];
	for (int i = 0; i < lc->moves; i++) {
		if (lc->move[i].move.coord == move.coord
		    && lc->move[i].move.color == move.color)
			return &lc->move[i].stats;
	}
	return NULL;
}

#endif
