#ifndef PACHI_LIBMAP_H
#define PACHI_LIBMAP_H

/* "Liberty map" - description of a particular liberty structure of a group.
 * The idea is that we can track local tactical effectivity of various moves
 * within the particular liberty structure context. */

#include <assert.h>
#include <stdbool.h>

#include "board.h"
#include "mq.h"
#include "stats.h"

#define LM_DEBUG if (0)

hash_t group_to_libmap(struct board *b, group_t group);


/* Setup of everything libmap-related. */

extern struct libmap_config {
	enum {
		LMP_THRESHOLD,
		LMP_UCB,
	} pick_mode;

	/* LMP_THRESHOLD: */
	/* Preference for moves of tactical rating over this threshold
	 * (...or unrated moves). */
	floating_t pick_threshold;
	/* In given percentage of cases, pick move regardless of its
	 * tactical rating.*/
	int pick_epsilon;
	/* Whether to rather skip this heuristic altogether than play
	 * badly performing move. */
	bool avoid_bad;

	/* LMP_UCB: */
	/* Exploration coefficient for the bandit. */
	floating_t explore_p;
	/* Default prior for considered moves. */
	struct move_stats prior, tenuki_prior;

	/* Whether to merge records for the same move taking care
	 * of different groups within the move queue. */
	bool mq_merge_groups;
	/* When checking move X, defending group A by counter-attacking
	 * group B, whether to use A, B or A^B as liberty map. */
	enum {
		LMC_DEFENSE = 1,
		LMC_ATTACK = 2,
		LMC_DEFENSE_ATTACK = 4,
	} counterattack;
	/* Whether to evaluate based on local or global result. */
	enum {
		LME_LOCAL,
		LME_LVALUE,
		LME_GLOBAL,
	} eval;
	/* Whether to also try and track tenuki moves. */
	bool tenuki;
} libmap_config;

void libmap_setup(char *arg);


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
	/* Multiple board instances may share the same libmap hash;
	 * on board_copy(), libmap is shared by default, so that all
	 * playouts reuse libmap of the master board. refcount keeps
	 * track of all the libmap uses in multi-thread environment. */
	int refcount;

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

/* Pick a move from @q, enqueue it in lmqueue and return its coordinate. */
static coord_t libmap_queue_mqpick(struct libmap_hash *lm, struct libmap_mq *lmqueue, struct libmap_mq *q);
/* Record queued moves in the hashtable based on final position of b and winner's color. */
void libmap_queue_process(struct libmap_hash *lm, struct libmap_mq *lmqueue, struct board *b, enum stone winner);
/* Add a result to the hashed statistics. */
void libmap_add_result(struct libmap_hash *lm, hash_t hash, struct move move, floating_t result, int playouts);
/* Get libmap context of a given group. */
static struct libmap_context *libmap_group_context(struct libmap_hash *lm, hash_t hash);
/* Get statistics of particular move in given libmap structure. */
static struct move_stats *libmap_move_stats(struct libmap_hash *lm, hash_t hash, struct move move);
/* Get statistics of particular move on given board. */
/* (Note that this is inherently imperfect as it does not take into account
 * counter-atari moves.) */
struct move_stats libmap_board_move_stats(struct libmap_hash *lm, struct board *b, struct move move);



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
		    && (libmap_config.mq_merge_groups
		        || (q->group[q->mq.moves - 1 - i].group == q->group[q->mq.moves - 1].group
		            && q->group[q->mq.moves - 1 - i].hash == q->group[q->mq.moves - 1].hash
		            && q->group[q->mq.moves - 1 - i].goal == q->group[q->mq.moves - 1].goal))) {
			q->mq.tag[q->mq.moves - 1 - i] |= q->mq.tag[q->mq.moves - 1];
			assert(q->color[q->mq.moves - 1 - i] == q->color[q->mq.moves - 1]);
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
		fprintf(stderr, "%s[%c:%s %"PRIhash"]", coord2sstr(q->mq.move[i], b),
			/* attacker / defender */
			board_at(b, q->group[i].group) == q->group[i].goal ? 'd' : 'a',
			coord2sstr(q->group[i].group, b),
			q->group[i].hash & libmap_hash_mask);
		struct move m = { .coord = q->mq.move[i], .color = q->color[i] };
		struct move_stats *ms = libmap_move_stats(b->libmap, q->group[i].hash, m);
		if (ms) {
			fprintf(stderr, "(%.3f/%d)", ms->value, ms->playouts);
		}
		fputc(' ', stderr);
	}
	fputc('\n', stderr);
}


static inline int
libmap_queue_mqpick_threshold(struct libmap_hash *lm, struct libmap_mq *q)
{
	/* Pick random move, up to a simple check - if a move has tactical
	 * rating lower than threshold, prefer another. */
	int p = fast_random(q->mq.moves);
	if (fast_random(100) < libmap_config.pick_epsilon)
		return p;

	bool found = false;
	unsigned int pp = p;
	do {
		int pm = p % q->mq.moves;
		struct move m = { .coord = q->mq.move[pm], .color = q->color[pm] };
		struct move_stats *ms = libmap_move_stats(lm, q->group[pm].hash, m);
		if (!ms || ms->value >= libmap_config.pick_threshold) {
			found = true;
			break;
		}
	} while (++p % q->mq.moves < pp);
	p %= q->mq.moves;
	if (!found && libmap_config.avoid_bad)
		return -1;
	return p;
}

static inline int
libmap_queue_mqpick_ucb(struct libmap_hash *lm, struct libmap_mq *q)
{
	int best_p = -1;
	floating_t best_urgency = -9999;
	LM_DEBUG fprintf(stderr, "\tBandit: ");

	for (unsigned int p = 0; p < q->mq.moves; p++) {
		struct libmap_context *lc = libmap_group_context(lm, q->group[p].hash);

		/* TODO: Consider all moves of this group,
		 * not just mq contents. */
		struct move m = { .coord = q->mq.move[p], .color = q->color[p] };
		struct move_stats s = !is_pass(m.coord) ? libmap_config.prior : libmap_config.tenuki_prior;
		struct move_stats *ms = libmap_move_stats(lm, q->group[p].hash, m);
		if (ms) stats_merge(&s, ms);

		int group_moves = s.playouts;
		if (lc) group_moves += lc->moves;

		floating_t urgency = s.value + libmap_config.explore_p * sqrt(log(group_moves) / s.playouts);
		LM_DEBUG fprintf(stderr, "%s[%.3f=%.3fx(%d/%d)] ", coord2sstr(m.coord, lm->b), urgency, s.value, group_moves, s.playouts);
		if (urgency > best_urgency) {
			best_p = (int) p;
			best_urgency = urgency;
		}
	}

	assert(best_p >= 0);
	LM_DEBUG fprintf(stderr, "\t=> %s\n", coord2sstr(q->mq.move[best_p], lm->b));
	return best_p;
}

static inline coord_t
libmap_queue_mqpick(struct libmap_hash *lm, struct libmap_mq *lmqueue, struct libmap_mq *q)
{
	if (!q->mq.moves)
		return pass; // nothing to do

	/* Create a list of groups involved in the MQ. */
	struct libmap_group *groups[MQL];
	unsigned int groups_n = 0;
	if (libmap_config.tenuki) {
		for (unsigned int i = 0; i < q->mq.moves; i++) {
			for (unsigned int j = 0; j < groups_n; j++)
				if (q->group[i].hash == groups[j]->hash)
					goto g_next_move;
			groups[groups_n++] = &q->group[i];
g_next_move:;
		}
	}

	/* Add tenuki move for each libmap group to the list of candidates. */
	/* XXX: Can the color vary within the queue? */
	if (libmap_config.tenuki) {
		struct move tenuki = { .coord = pass, .color = q->color[0] };
		for (unsigned int i = 0; i < groups_n; i++)
			libmap_mq_add(q, tenuki, 0 /* XXX */, *groups[i]);
	}

	unsigned int p = 0;
	if (q->mq.moves > 1) {
		if (lm) {
			switch (libmap_config.pick_mode) {
			case LMP_THRESHOLD:
				p = libmap_queue_mqpick_threshold(lm, q);
				break;
			case LMP_UCB:
				p = libmap_queue_mqpick_ucb(lm, q);
				break;
			}
		} else {
			p = fast_random(q->mq.moves);
		}
	}
	if (p < 0)
		return pass;

	if (lm) {
		struct move m = { .coord = q->mq.move[p], .color = q->color[p] };
		libmap_mq_add(lmqueue, m, q->mq.tag[p], q->group[p]);
	}

	return q->mq.move[p];
}

static inline struct libmap_context *
libmap_group_context(struct libmap_hash *lm, hash_t hash)
{
	hash_t ih;
	for (ih = hash; lm->hash[ih & libmap_hash_mask].hash != hash; ih++) {
		if (lm->hash[ih & libmap_hash_mask].moves == 0)
			return NULL;
		if (ih >= hash + libmap_hash_maxline)
			return NULL;
	}
	return &lm->hash[ih & libmap_hash_mask];
}

static inline struct move_stats *
libmap_move_stats(struct libmap_hash *lm, hash_t hash, struct move move)
{
	struct libmap_context *lc = libmap_group_context(lm, hash);
	if (!lc) return NULL;
	for (int i = 0; i < lc->moves; i++) {
		if (lc->move[i].move.coord == move.coord
		    && lc->move[i].move.color == move.color)
			return &lc->move[i].stats;
	}
	return NULL;
}

#endif
