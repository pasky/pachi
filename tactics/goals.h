#ifndef PACHI_TACTICS_GOALS_H
#define PACHI_TACTICS_GOALS_H

/* Infrastructure for libmap-based goal evaluation of moves. */

#include "libmap.h"

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


/** Infrastructure for tracking moves to be confronted with the libmap hash. */

/* Our own version of move_queue, but including liberty maps of moves. */
/* The user will usually first create a queue of tactical goals and pick
 * (using libmap_mq functions below), then add that one to libmap_hash's
 * global move queue, processed at the end of the whole playout. */

struct libmap_move_groupinfo {
	/* Group-relative tactical description of a move. */
	enum stone color;
	group_t group;
	hash_t hash;
	enum stone goal;
};

struct libmap_mq {
	struct move_queue mq;
	enum stone color[MQL]; // complements mq.move
	struct libmap_move_groupinfo gi[MQL];
};

/* libmap_mq_pick() would be simple fast_random(mq.moves), but c.f.
 * libmap_queue_mqpick() below. */
static void libmap_mq_add(struct libmap_mq *q, struct move m, unsigned char tag, struct libmap_move_groupinfo lmgi);
static void libmap_mq_nodup(struct libmap_mq *q);
static void libmap_mq_print(struct libmap_mq *q, struct board *b, char *label);


/** Global storage of all the libmap contexts encountered. */

struct libmap_group;

struct libmap_hash {
	struct board *b;
	/* Multiple board instances may share the same libmap hash;
	 * on board_copy(), libmap is shared by default, so that all
	 * playouts reuse libmap of the master board. refcount keeps
	 * track of all the libmap uses in multi-thread environment. */
	int refcount;

	/* All groups existing on a "base position" (in case of the UCT
	 * engine, in some tree branch) will have their struct libmap_group
	 * pre-allocated. When recording moves for libmap groups based on
	 * playout data, no new libmap_group records are used and data on
	 * groups existing only in simulations is not kept. */
	struct libmap_group **groups[2]; // [color-1][board_size2(b)]
};

/* Get a new libmap. */
struct libmap_hash *libmap_init(struct board *b);
/* Release libmap. Based on refcount, this will free it. */
void libmap_put(struct libmap_hash *lm);

struct libmap_group {
	group_t group;
	enum stone color;

	/* Stored per-group libmap contexts with statistics of moves
	 * performance regarding achieving a tactical goal related
	 * to this group (move by us == survival, move by opponent
	 * == kill). */
	/* We store statistics in a hash table without separated chains;
	 * if bucket is occupied, we look into the following ones,
	 * allowing up to libmap_hash_maxline subsequent checks. */
	/* XXX: We mishandle hashes >= UINT64_MAX - libmap_hash_maxline. */
#define libmap_hash_bits 11
#define libmap_hash_size (1 << libmap_hash_bits)
#define libmap_hash_mask (libmap_hash_size - 1)
#define libmap_hash_maxline 32
	struct libmap_context hash[libmap_hash_size];
};

/* Allocate a libmap group record for the given group. */
void libmap_group_init(struct libmap_hash *lm, struct board *b, group_t g, enum stone color);

/* Pick a move from @q, enqueue it in lmqueue and return its coordinate. */
static coord_t libmap_queue_mqpick(struct board *b, struct libmap_mq *q);
/* Record queued moves in the hashtable based on final position of b and winner's color. */
void libmap_queue_process(struct board *b, enum stone winner);

/* Add a result to the hashed statistics. */
void libmap_add_result(struct libmap_hash *lm, struct libmap_group *lg, hash_t hash, struct move move, floating_t result, int playouts);
/* Get libmap context of a given group. */
static struct libmap_context *libmap_group_context(struct libmap_hash *lm, struct libmap_group *lg, hash_t hash);
/* Get statistics of particular move in given libmap structure. */
static struct move_stats *libmap_move_stats(struct libmap_hash *lm, struct libmap_group *lg, hash_t hash, struct move move);
/* Get statistics of particular move on given board. */
/* (Note that this is inherently imperfect as it does not take into account
 * counter-atari moves.) */
struct move_stats libmap_board_move_stats(struct libmap_hash *lm, struct board *b, struct move move);



static inline void
libmap_mq_add(struct libmap_mq *q, struct move m, unsigned char tag, struct libmap_move_groupinfo lmgi)
{
	assert(q->mq.moves < MQL);
	q->mq.tag[q->mq.moves] = tag;
	q->mq.move[q->mq.moves] = m.coord;
	q->color[q->mq.moves] = m.color;
	q->gi[q->mq.moves] = lmgi;
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
		        || (q->gi[q->mq.moves - 1 - i].group == q->gi[q->mq.moves - 1].group
		            && q->gi[q->mq.moves - 1 - i].hash == q->gi[q->mq.moves - 1].hash
		            && q->gi[q->mq.moves - 1 - i].goal == q->gi[q->mq.moves - 1].goal))) {
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
			board_at(b, q->gi[i].group) == q->gi[i].goal ? 'd' : 'a',
			coord2sstr(q->gi[i].group, b),
			q->gi[i].hash & libmap_hash_mask);
		struct move m = { .coord = q->mq.move[i], .color = q->color[i] };
		struct libmap_group *lg = b->libmap->groups[q->gi[i].color - 1][q->gi[i].group];
		struct move_stats *ms = libmap_move_stats(b->libmap, lg, q->gi[i].hash, m);
		if (ms) {
			fprintf(stderr, "(%.3f/%d)", ms->value, ms->playouts);
		}
		fputc(' ', stderr);
	}
	fputc('\n', stderr);
}


static inline int
libmap_queue_mqpick_threshold(struct libmap_hash *lm, struct board *b, struct libmap_mq *q)
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
		struct libmap_group *lg = lm->groups[q->gi[pm].color - 1][q->gi[pm].group];
		struct move_stats *ms = libmap_move_stats(lm, lg, q->gi[pm].hash, m);
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
libmap_queue_mqpick_ucb(struct libmap_hash *lm, struct board *b, struct libmap_mq *q)
{
	int best_pa[BOARD_MAX_MOVES + 1], best_pn = 1;
	floating_t best_urgency = -9999;
	LM_DEBUG fprintf(stderr, "\tBandit: ");

	for (unsigned int p = 0; p < q->mq.moves; p++) {
		/* TODO: Consider all moves of this group,
		 * not just mq contents. */
		struct move m = { .coord = q->mq.move[p], .color = q->color[p] };
		//fprintf(stderr, "%d: %d, %d\n", p, q->gi[p].color - 1, q->gi[p].group);
		struct libmap_group *lg = lm->groups[q->gi[p].color - 1][q->gi[p].group];
		struct libmap_context *lc = libmap_group_context(lm, lg, q->gi[p].hash);

		struct move_stats s = !is_pass(m.coord) ? libmap_config.prior : libmap_config.tenuki_prior;
		int group_visits = (lc ? lc->visits : 0) + s.playouts;
		struct move_stats *ms = libmap_move_stats(lm, lg, q->gi[p].hash, m);
		if (ms) stats_merge(&s, ms);

		floating_t urgency = s.value + libmap_config.explore_p * sqrt(log(group_visits) / s.playouts);
		LM_DEBUG fprintf(stderr, "%s[%.3f=%.3fx(%d/%d)] ", coord2sstr(m.coord, lm->b), urgency, s.value, group_visits, s.playouts);
		if (urgency > best_urgency) {
			best_pn = 1;
			best_pa[0] = (int) p;
			best_urgency = urgency;

		} else if (urgency == best_urgency) {
			best_pa[best_pn++] = (int) p;
		}
	}

	int best_p = best_pa[fast_random(best_pn)];
	assert(best_p >= 0);
	LM_DEBUG fprintf(stderr, "\t=[%d]> %s\n", best_pn, coord2sstr(q->mq.move[best_p], lm->b));
	return best_p;
}

static inline coord_t
libmap_queue_mqpick(struct board *b, struct libmap_mq *q)
{
	if (!q->mq.moves)
		return pass; // nothing to do

	/* Create a list of groups involved in the MQ. */
	struct libmap_move_groupinfo *lmgi[MQL];
	unsigned int lmgi_n = 0;
	if (libmap_config.tenuki) {
		for (unsigned int i = 0; i < q->mq.moves; i++) {
			for (unsigned int j = 0; j < lmgi_n; j++)
				if (q->gi[i].hash == lmgi[j]->hash)
					goto g_next_move;
			lmgi[lmgi_n++] = &q->gi[i];
g_next_move:;
		}
	}

	/* Add tenuki move for each libmap group to the list of candidates. */
	/* XXX: Can the color vary within the queue? */
	if (libmap_config.tenuki) {
		struct move tenuki = { .coord = pass, .color = q->color[0] };
		for (unsigned int i = 0; i < lmgi_n; i++)
			libmap_mq_add(q, tenuki, 0 /* XXX */, *lmgi[i]);
	}

	unsigned int p = 0;
	if (q->mq.moves > 1) {
		if (b->libmap) {
			switch (libmap_config.pick_mode) {
			case LMP_THRESHOLD:
				p = libmap_queue_mqpick_threshold(b->libmap, b, q);
				break;
			case LMP_UCB:
				p = libmap_queue_mqpick_ucb(b->libmap, b, q);
				break;
			}
		} else {
			p = fast_random(q->mq.moves);
		}
	}
	if (p < 0)
		return pass;

	if (b->libmap) {
		struct move m = { .coord = q->mq.move[p], .color = q->color[p] };
		libmap_mq_add(b->lmqueue, m, q->mq.tag[p], q->gi[p]);
	}

	return q->mq.move[p];
}

static inline struct libmap_context *
libmap_group_context(struct libmap_hash *lm, struct libmap_group *lg, hash_t hash)
{
	if (!lg) return NULL;
	hash_t ih;
	for (ih = hash; lg->hash[ih & libmap_hash_mask].hash != hash; ih++) {
		if (lg->hash[ih & libmap_hash_mask].moves == 0)
			return NULL;
		if (ih >= hash + libmap_hash_maxline)
			return NULL;
	}
	return &lg->hash[ih & libmap_hash_mask];
}

static inline struct move_stats *
libmap_move_stats(struct libmap_hash *lm, struct libmap_group *lg, hash_t hash, struct move move)
{
	struct libmap_context *lc = libmap_group_context(lm, lg, hash);
	if (!lc) return NULL;
	for (int i = 0; i < lc->moves; i++) {
		if (lc->move[i].move.coord == move.coord
		    && lc->move[i].move.color == move.color)
			return &lc->move[i].stats;
	}
	return NULL;
}

#endif
