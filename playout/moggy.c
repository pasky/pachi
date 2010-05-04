/* Playout policy by stochastically applying a fixed set of decision
 * rules in given order - modelled after the intelligent playouts
 * in the Mogo engine. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "pattern3.h"
#include "playout.h"
#include "playout/moggy.h"
#include "random.h"
#include "tactics.h"
#include "uct/prior.h"

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)

/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


/* Note that the context can be shared by multiple threads! */

struct moggy_policy {
	bool ladders, ladderassess, borderladders, assess_local;
	int lcapturerate, atarirate, capturerate, patternrate, korate;
	int selfatarirate, alwaysccaprate;
	int fillboardtries;
	int koage;
	/* Whether to look for patterns around second-to-last move. */
	bool pattern2;

	struct pattern3s patterns;
};


struct group_state {
	enum {
		G_ATARI,
		G_2LIB, /* Unused. */
		G_SAFE /* Unused. */
	} status:2;

	/* Below, we keep track of each trait for each |color_to_play */
	int capturable_ready:2; // is @capturable meaningful?
	int capturable:2;

	int can_countercapture_ready:2;
	int can_countercapture:2;
};

/* Cache of evaluation of various board features. */
struct board_state {
	int bsize2;
	hash_t hash;
	struct group_state *groups; /* [board_size2()], indexed by group_t */
	unsigned char *groups_known; /* Bitmap of known groups. */
};

/* Using board cache: this turns out to be actually a 10% slowdown,
 * since we reuse data in the cache only very little within single
 * move. */
// #define CACHE_STATE
/* Reusing board cache across moves if they are successive on the
 * board; only cache entries within cfg distance 2 of the last move
 * are cleared. */
// #define PERSISTENT_STATE

#ifdef CACHE_STATE
static __thread struct board_state *ss;

static bool
board_state_reuse(struct board_state *s, struct board *b)
{
	/* Decide how much of the board state we can reuse. */
	/* We do not cache ladder decisions, so we don't have
	 * to worry about this. */
	coord_t c = b->last_move.coord;

	if (unlikely(is_pass(c))) {
		/* Passes don't change anything. */
		return true;
	}

	if (unlikely(board_at(b, c) == S_NONE)) {
		/* Suicide is hopeless. */
		return false;
	}

	/* XXX: we can make some moves self-atari. */

	if (neighbor_count_at(b, c, S_BLACK) + neighbor_count_at(b, c, S_WHITE) == 0) {
		/* We are not taking off liberties of any other stones. */
		return true;
	}

	return false;
}

static inline struct board_state *
board_state_init(struct board *b)
{
	if (ss) {
		if (ss->bsize2 != board_size2(b)) {
			free(ss->groups);
			free(ss->groups_known);
			free(ss); ss = NULL;
		}
#ifdef PERSISTENT_STATE
		/* Only one stone added to the board, nothing removed. */
		else if (ss->hash == (b->hash ^ hash_at(b, b->last_move.coord, b->last_move.color))) {
			ss->hash = b->hash;
			if (likely(board_state_reuse(ss, b)))
				return ss;
		}
#endif
	}
	if (!ss) {
		ss = malloc2(sizeof(*ss));
		ss->bsize2 = board_size2(b);
		ss->groups = malloc2(board_size2(b) * sizeof(*ss->groups));
		ss->groups_known = malloc2(board_size2(b) / 8 + 1);
	}
	ss->hash = b->hash;
	memset(ss->groups_known, 0, board_size2(b) / 8 + 1);
	return ss;
}

#define group_is_known(s, g) (s->groups_known[g >> 3] & (1 << (g & 7)))
#define group_set_known(s, g) (s->groups_known[g >> 3] |= (1 << (g & 7)))
#define group_trait_ready(s, g, color, gstat, trait) do { \
	if (!group_is_known(s, g)) { \
		memset(&s->groups[g], 0, sizeof(s->groups[g])); \
		group_set_known(s, g); \
	} \
	s->groups[g].status = gstat; \
	s->groups[g].trait ## _ready |= color; \
} while (0)
#define group_trait_is_ready(s, g, color, trait) (s->groups[g].trait ## _ready & color)
#define group_trait_set(s, g, color, trait, val) s->groups[g].trait = (s->groups[g].trait & ~color) | (!!val * color)
#define group_trait_get(s, g, color, trait) (s->groups[g].trait & color)

#else

#define board_state_init(b) NULL
#define group_is_known(s, g) false
#define group_set_known(s, g)
#define group_trait_ready(s, g, color, gstat, trait)
#define group_trait_is_ready(s, g, color, trait) false
#define group_trait_set(s, g, color, trait, val)
#define group_trait_get(s, g, color, trait) false
#endif


static char moggy_patterns_src[][11] = {
	/* hane pattern - enclosing hane */
	"XOX"
	"..."
	"???",
	/* hane pattern - non-cutting hane */
	"XO."
	"..."
	"?.?",
	/* hane pattern - magari */
	"XO?"
	"X.."
	"x.?",
	/* hane pattern - thin hane */
	"XOO"
	"..."
	"?.?" "X",
	/* generic pattern - katatsuke or diagonal attachment; similar to magari */
	".O."
	"X.."
	"...",
	/* cut1 pattern (kiri) - unprotected cut */
	"XO?"
	"O.o"
	"?o?",
	/* cut1 pattern (kiri) - peeped cut */
	"XO?"
	"O.X"
	"???",
	/* cut2 pattern (de) */
	"?X?"
	"O.O"
	"ooo",
	/* cut keima (not in Mogo) */
	"OX?"
	"o.O"
	"???", /* o?? has some pathological tsumego cases */
	/* side pattern - chase */
	"X.?"
	"O.?"
	"##?",
	/* side pattern - weirdness (SUSPICIOUS) */
	"?X?"
	"X.O"
	"###",
	/* side pattern - sagari (SUSPICIOUS) */
	"?XO"
	"x.x" /* Mogo has "x.?" */
	"###" /* Mogo has "X" */,
	/* side pattern - throw-in (SUSPICIOUS) */
#if 0
	"?OX"
	"o.O"
	"?##" "X",
#endif
	/* side pattern - cut (SUSPICIOUS) */
	"?OX"
	"X.O"
	"###" /* Mogo has "X" */,
};
#define moggy_patterns_src_n sizeof(moggy_patterns_src) / sizeof(moggy_patterns_src[0])

static inline bool
test_pattern3_here(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;
	/* Check if 3x3 pattern is matched by given move... */
	if (!pattern3_move_here(&pp->patterns, b, m))
		return false;
	/* ...and the move is not obviously stupid. */
	if (is_bad_selfatari(b, m->color, m->coord))
		return false;
	/* Ladder moves are stupid. */
	group_t atari_neighbor = board_get_atari_neighbor(b, m->coord, m->color);
	if (atari_neighbor && is_ladder(b, m->coord, atari_neighbor, pp->borderladders, pp->ladders))
		return false;
	return true;
}

static void
apply_pattern_here(struct playout_policy *p, struct board *b, coord_t c, enum stone color, struct move_queue *q)
{
	struct move m2 = { .coord = c, .color = color };
	if (board_is_valid_move(b, &m2) && test_pattern3_here(p, b, &m2))
		mq_add(q, c);
}

/* Check if we match any pattern around given move (with the other color to play). */
static coord_t
apply_pattern(struct playout_policy *p, struct board *b, struct move *m, struct move *mm)
{
	struct move_queue q;
	q.moves = 0;

	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return pass;

	foreach_8neighbor(b, m->coord) {
		apply_pattern_here(p, b, c, stone_other(m->color), &q);
	} foreach_8neighbor_end;

	if (mm) { /* Second move for pattern searching */
		foreach_8neighbor(b, mm->coord) {
			if (coord_is_8adjecent(m->coord, c, b))
				continue;
			apply_pattern_here(p, b, c, stone_other(m->color), &q);
		} foreach_8neighbor_end;
	}

	if (PLDEBUGL(5))
		mq_print(&q, b, "Pattern");

	return mq_pick(&q);
}


static bool
can_play_on_lib(struct playout_policy *p, struct board_state *s,
                struct board *b, group_t g, enum stone to_play)
{
	if (group_is_known(s, g) && group_trait_is_ready(s, g, to_play, capturable)) {
		/* We have already seen this group. */
		assert(s->groups[g].status == G_ATARI);
		if (group_trait_get(s, g, to_play, capturable))
			return true;
		else
			return false;
	}

	/* Cache miss. Set up cache entry, default at capturable = false. */
	group_trait_ready(s, g, to_play, G_ATARI, capturable);

	coord_t capture = board_group_info(b, g).lib[0];
	if (PLDEBUGL(6))
		fprintf(stderr, "can capture group %d (%s)?\n",
			g, coord2sstr(capture, b));
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, capture)
	    && !is_bad_selfatari(b, to_play, capture)) {
		group_trait_set(s, g, to_play, capturable, true);
		return true;
	}

	return false;
}

/* For given position @c, decide if this is a group that is in danger from
 * @capturer and @to_play can do anything about it (play at the last
 * liberty to either capture or escape). */
/* Note that @to_play is important; e.g. consider snapback, it's good
 * to play at the last liberty by attacker, but not defender. */
static __attribute__((always_inline)) bool
capturable_group(struct playout_policy *p, struct board_state *s,
                 struct board *b, enum stone capturer, coord_t c,
		 enum stone to_play)
{
	group_t g = group_at(b, c);
	if (likely(board_at(b, c) != stone_other(capturer)
	           || board_group_info(b, g).libs > 1))
		return false;

	return can_play_on_lib(p, s, b, g, to_play);
}

/* For given atari group @group owned by @owner, decide if @to_play
 * can save it / keep it in danger by dealing with one of the
 * neighboring groups. */
static bool
can_countercapture(struct playout_policy *p, struct board_state *s,
                   struct board *b, enum stone owner, group_t g,
		   enum stone to_play, struct move_queue *q)
{
	if (b->clen < 2)
		return false;
	if (group_is_known(s, g) && group_trait_is_ready(s, g, to_play, can_countercapture)) {
		/* We have already seen this group. */
		assert(s->groups[g].status == G_ATARI);
		if (group_trait_get(s, g, to_play, can_countercapture)) {
			if (q) { /* Scan for countercapture liberties. */
				goto scan;
			}
			return true;
		} else {
			return false;
		}
	}

	/* Cache miss. Set up cache entry, default at can_countercapture = true. */
	group_trait_ready(s, g, to_play, G_ATARI, can_countercapture);
	group_trait_set(s, g, to_play, can_countercapture, true);

scan:;
	int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, g) {
		foreach_neighbor(b, c, {
			if (!capturable_group(p, s, b, owner, c, to_play))
				continue;

			if (!q) {
				return true;
			}
			mq_add(q, board_group_info(b, group_at(b, c)).lib[0]);
			mq_nodup(q);
		});
	} foreach_in_group_end;

	bool can = q ? q->moves > qmoves_prev : false;
	group_trait_set(s, g, to_play, can_countercapture, can);
	return can;
}

#ifdef NO_DOOMED_GROUPS
static bool
can_be_rescued(struct playout_policy *p, struct board_state *s,
               struct board *b, group_t group, enum stone color)
{
	/* Does playing on the liberty rescue the group? */
	if (can_play_on_lib(p, s, b, group, color))
		return true;

	/* Then, maybe we can capture one of our neighbors? */
	return can_countercapture(p, s, b, color, group, color, NULL);
}
#endif

/* ladder != NULL implies to always enqueue all relevant moves. */
static void
group_atari_check(struct playout_policy *p, struct board *b, group_t group, enum stone to_play,
                  struct move_queue *q, coord_t *ladder, struct board_state *s)
{
	struct moggy_policy *pp = p->data;
	int qmoves_prev = q->moves;

	/* We don't use @to_play almost anywhere since any moves here are good
	 * for both defender and attacker. */

	enum stone color = board_at(b, group_base(group));
	coord_t lib = board_group_info(b, group).lib[0];

	assert(color != S_OFFBOARD && color != S_NONE);
	if (PLDEBUGL(5))
		fprintf(stderr, "[%s] atariiiiiiiii %s of color %d\n",
		        coord2sstr(group, b), coord2sstr(lib, b), color);
	assert(board_at(b, lib) == S_NONE);

	/* Do not bother with kos. */
	if (group_is_onestone(b, group)
	    && neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) == 4)
		return;

	/* Can we capture some neighbor? */
	bool ccap = can_countercapture(p, s, b, color, group, to_play, q);
	if (ccap && !ladder && pp->alwaysccaprate > fast_random(100))
		return;

	/* Do not suicide... */
	if (!can_play_on_lib(p, s, b, group, to_play))
		return;
#ifdef NO_DOOMED_GROUPS
	/* Do not remove group that cannot be saved by the opponent. */
	if (to_play != color && !can_be_rescued(p, s, b, group, color))
		return;
#endif
	if (PLDEBUGL(6))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders. */
	if (is_ladder(b, lib, group, pp->borderladders, pp->ladders)) {
		/* Sometimes we want to keep the ladder move in the
		 * queue in order to discourage it. */
		if (!ladder)
			return;
		else
			*ladder = lib;
	}
	if (PLDEBUGL(6))
		fprintf(stderr, "...no ladder\n");

	if (to_play != color) {
		/* We are the attacker! In that case, throw away the moves
		 * that defend our groups, since we can capture the culprit. */
		q->moves = qmoves_prev;
	}

	mq_add(q, lib);
	mq_nodup(q);
}

static coord_t
global_atari_check(struct playout_policy *p, struct board *b, enum stone to_play, struct board_state *s)
{
	struct move_queue q;
	q.moves = 0;

	if (b->clen == 0)
		return pass;

	int g_base = fast_random(b->clen);
	for (int g = g_base; g < b->clen; g++) {
		group_atari_check(p, b, group_at(b, group_base(b->c[g])), to_play, &q, NULL, s);
		if (q.moves > 0)
			return mq_pick(&q);
	}
	for (int g = 0; g < g_base; g++) {
		group_atari_check(p, b, group_at(b, group_base(b->c[g])), to_play, &q, NULL, s);
		if (q.moves > 0)
			return mq_pick(&q);
	}
	return pass;
}

static coord_t
local_atari_check(struct playout_policy *p, struct board *b, struct move *m, struct board_state *s)
{
	struct move_queue q;
	q.moves = 0;

	/* Did the opponent play a self-atari? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 1) {
		group_atari_check(p, b, group_at(b, m->coord), stone_other(m->color), &q, NULL, s);
	}

	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;
		group_atari_check(p, b, g, stone_other(m->color), &q, NULL, s);
	});

	if (PLDEBUGL(5))
		mq_print(&q, b, "Local atari");

	return mq_pick(&q);
}

static bool
miai_2lib(struct board *b, group_t group, enum stone color)
{
	bool can_connect = false, can_pull_out = false;
	/* We have miai if we can either connect on both libs,
	 * or connect on one lib and escape on another. (Just
	 * having two escape routes can be risky.) We must make
	 * sure that we don't consider following as miai:
	 * X X X O
	 * X . . O
	 * O O X O - left dot would be pull-out, right dot connect */
	foreach_neighbor(b, board_group_info(b, group).lib[0], {
		enum stone cc = board_at(b, c);
		if (cc == S_NONE && cc != board_at(b, board_group_info(b, group).lib[1])) {
			can_pull_out = true;
		} else if (cc != color) {
			continue;
		}

		group_t cg = group_at(b, c);
		if (cg && cg != group && board_group_info(b, cg).libs > 1)
			can_connect = true;
	});
	foreach_neighbor(b, board_group_info(b, group).lib[1], {
		enum stone cc = board_at(b, c);
		if (c == board_group_info(b, group).lib[0])
			continue;
		if (cc == S_NONE && can_connect) {
			return true;
		} else if (cc != color) {
			continue;
		}

		group_t cg = group_at(b, c);
		if (cg && cg != group && board_group_info(b, cg).libs > 1)
			return (can_connect || can_pull_out);
	});
	return false;
}

static void
check_group_atari(struct board *b, group_t group, enum stone owner,
		  enum stone to_play, struct move_queue *q)
{
	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, group).lib[i];
		assert(board_at(b, lib) == S_NONE);
		if (!board_is_valid_play(b, to_play, lib))
			continue;

		/* Don't play at the spot if it is extremely short
		 * of liberties... */
		/* XXX: This looks harmful, could significantly
		 * prefer atari to throwin:
		 *
		 * XXXOOOOOXX
		 * .OO.....OX
		 * XXXOOOOOOX */
#if 0
		if (neighbor_count_at(b, lib, stone_other(owner)) + immediate_liberty_count(b, lib) < 2)
			continue;
#endif

#ifdef NO_DOOMED_GROUPS
		/* If the owner can't play at the spot, we don't want
		 * to bother either. */
		if (is_bad_selfatari(b, owner, lib))
			continue;
#endif

		/* Of course we don't want to play bad selfatari
		 * ourselves, if we are the attacker... */
		if (
#ifdef NO_DOOMED_GROUPS
		    to_play != owner &&
#endif
		    is_bad_selfatari(b, to_play, lib))
			continue;

		/* Tasty! Crispy! Good! */
		mq_add(q, lib);
		mq_nodup(q);
	}
}

static void
group_2lib_check(struct playout_policy *p, struct board *b, group_t group, enum stone to_play,
                 struct move_queue *q, struct board_state *s)
{
	enum stone color = board_at(b, group_base(group));
	assert(color != S_OFFBOARD && color != S_NONE);

	if (PLDEBUGL(5))
		fprintf(stderr, "[%s] 2lib check of color %d\n",
			coord2sstr(group, b), color);

	/* Do not try to atari groups that cannot be harmed. */
	if (miai_2lib(b, group, color))
		return;

	check_group_atari(b, group, color, to_play, q);

	/* Can we counter-atari another group, if we are the defender? */
	if (to_play != color)
		return;
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != stone_other(color))
				continue;
			group_t g2 = group_at(b, c);
			if (board_group_info(b, g2).libs != 2)
				continue;
			check_group_atari(b, g2, color, to_play, q);
		});
	} foreach_in_group_end;
}

static coord_t
local_2lib_check(struct playout_policy *p, struct board *b, struct move *m, struct board_state *s)
{
	struct move_queue q;
	q.moves = 0;

	/* Does the opponent have just two liberties? */
	if (board_group_info(b, group_at(b, m->coord)).libs == 2) {
		group_2lib_check(p, b, group_at(b, m->coord), stone_other(m->color), &q, s);
#if 0
		/* We always prefer to take off an enemy chain liberty
		 * before pulling out ourselves. */
		/* XXX: We aren't guaranteed to return to that group
		 * later. */
		if (q.moves)
			return q.move[fast_random(q.moves)];
#endif
	}

	/* Then he took a third liberty from neighboring chain? */
	foreach_neighbor(b, m->coord, {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 2)
			continue;
		group_2lib_check(p, b, g, stone_other(m->color), &q, s);
	});

	if (PLDEBUGL(5))
		mq_print(&q, b, "Local 2lib");

	return mq_pick(&q);
}

coord_t
playout_moggy_choose(struct playout_policy *p, struct board *b, enum stone to_play)
{
	struct moggy_policy *pp = p->data;
	coord_t c;

	struct board_state *s = board_state_init(b);

	if (PLDEBUGL(5))
		board_print(b, stderr);

	/* Ko fight check */
	if (!is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage
	    && pp->korate > fast_random(100)) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			return b->last_ko.coord;
	}

	/* Local checks */
	if (!is_pass(b->last_move.coord)) {
		/* Local group in atari? */
		if (pp->lcapturerate > fast_random(100)) {
			c = local_atari_check(p, b, &b->last_move, s);
			if (!is_pass(c))
				return c;
		}

		/* Local group can be PUT in atari? */
		if (pp->atarirate > fast_random(100)) {
			c = local_2lib_check(p, b, &b->last_move, s);
			if (!is_pass(c))
				return c;
		}

		/* Check for patterns we know */
		if (pp->patternrate > fast_random(100)) {
			c = apply_pattern(p, b, &b->last_move,
			                  pp->pattern2 && b->last_move2.coord >= 0 ? &b->last_move2 : NULL);
			if (!is_pass(c))
				return c;
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > fast_random(100)) {
		c = global_atari_check(p, b, to_play, s);
		if (!is_pass(c))
			return c;
	}

	/* Fill board */
	int fbtries = b->flen / 8;
	for (int i = 0; i < (fbtries < pp->fillboardtries ? fbtries : pp->fillboardtries); i++) {
		coord_t coord = b->f[fast_random(b->flen)];
		if (immediate_liberty_count(b, coord) != 4)
			continue;
		foreach_diag_neighbor(b, coord) {
			if (board_at(b, c) != S_NONE)
				goto next_try;
		} foreach_diag_neighbor_end;
		return coord;
next_try:;
	}

	return pass;
}


static int
assess_local_bonus(struct playout_policy *p, struct board *board, coord_t a, coord_t b, int games)
{
	struct moggy_policy *pp = p->data;
	if (!pp->assess_local)
		return games;

	int dx = abs(coord_x(a, board) - coord_x(b, board));
	int dy = abs(coord_y(a, board) - coord_y(b, board));
	/* adjecent move, directly or diagonally? */
	if (dx + dy <= 1 + (dx && dy))
		return games;
	else
		return games / 2;
}

void
playout_moggy_assess_group(struct playout_policy *p, struct prior_map *map, group_t g, int games,
                           struct board_state *s)
{
	struct moggy_policy *pp = p->data;
	struct board *b = map->b;
	struct move_queue q; q.moves = 0;

	if (board_group_info(b, g).libs > 2)
		return;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "ASSESS of group %s:\n", coord2sstr(g, b));
		board_print(b, stderr);
	}

	if (board_group_info(b, g).libs == 2) {
		if (!pp->atarirate)
			return;
		group_2lib_check(p, b, g, map->to_play, &q, s);
		while (q.moves--) {
			coord_t coord = q.move[q.moves];
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: 2lib %s\n", coord2sstr(coord, b));
			int assess = assess_local_bonus(p, b, b->last_move.coord, coord, games) / 2;
			add_prior_value(map, coord, 1, assess);
		}
		return;
	}

	/* This group, sir, is in atari! */

	if (!pp->capturerate && !pp->lcapturerate && !pp->ladderassess)
		return;

	coord_t ladder = pass;
	group_atari_check(p, b, g, map->to_play, &q, &ladder, s);
	while (q.moves--) {
		coord_t coord = q.move[q.moves];

		/* _Never_ play here if this move plays out
		 * a caught ladder. */
		if (coord == ladder && !board_playing_ko_threat(b)) {
			/* Note that the opposite is not guarded against;
			 * we do not advise against capturing a laddered
			 * group (but we don't encourage it either). Such
			 * a move can simplify tactical situations if we
			 * can afford it. */
			if (!pp->ladderassess || map->to_play != board_at(b, g))
				continue;
			/* FIXME: We give the malus even if this move
			 * captures another group. */
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: ladder %s\n", coord2sstr(coord, b));
			add_prior_value(map, coord, 0, games);
			continue;
		}

		if (!pp->capturerate && !pp->lcapturerate)
			continue;

		if (PLDEBUGL(5))
			fprintf(stderr, "1.0: atari %s\n", coord2sstr(coord, b));
		int assess = assess_local_bonus(p, b, b->last_move.coord, coord, games) * 2;
		add_prior_value(map, coord, 1, assess);
	}
}

void
playout_moggy_assess_one(struct playout_policy *p, struct prior_map *map, coord_t coord, int games)
{
	struct moggy_policy *pp = p->data;
	struct board *b = map->b;

	if (PLDEBUGL(5)) {
		fprintf(stderr, "ASSESS of move %s:\n", coord2sstr(coord, b));
		board_print(b, stderr);
	}

	/* Is this move a self-atari? */
	if (pp->selfatarirate) {
		if (!board_playing_ko_threat(b) && is_bad_selfatari(b, map->to_play, coord)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "0.0: self-atari\n");
			add_prior_value(map, coord, 0, games);
			return;
		}
	}

	/* Pattern check */
	if (pp->patternrate) {
		struct move m = { .color = map->to_play, .coord = coord };
		if (test_pattern3_here(p, b, &m)) {
			if (PLDEBUGL(5))
				fprintf(stderr, "1.0: pattern\n");
			int assess = assess_local_bonus(p, b, b->last_move.coord, coord, games);
			add_prior_value(map, coord, 1, assess);
		}
	}

	return;
}

void
playout_moggy_assess(struct playout_policy *p, struct prior_map *map, int games)
{
	struct moggy_policy *pp = p->data;

	struct board_state *s = board_state_init(map->b);

	/* First, go through all endangered groups. */
	if (pp->lcapturerate || pp->capturerate || pp->atarirate || pp->ladderassess)
		for (group_t g = 1; g < board_size2(map->b); g++)
			if (group_at(map->b, g) == g)
				playout_moggy_assess_group(p, map, g, games, s);

	/* Then, assess individual moves. */
	if (!pp->patternrate && !pp->selfatarirate)
		return;
	foreach_point(map->b) {
		if (map->consider[c])
			playout_moggy_assess_one(p, map, c, games);
	} foreach_point_end;
}

bool
playout_moggy_permit(struct playout_policy *p, struct board *b, struct move *m)
{
	struct moggy_policy *pp = p->data;

	/* The idea is simple for now - never allow self-atari moves.
	 * They suck in general, but this also permits us to actually
	 * handle seki in the playout stage. */

	if (fast_random(100) >= pp->selfatarirate) {
		if (PLDEBUGL(5))
			fprintf(stderr, "skipping sar test\n");
		return true;
	}
	bool selfatari = is_bad_selfatari(b, m->color, m->coord);
	if (PLDEBUGL(5) && selfatari)
		fprintf(stderr, "__ Prohibiting self-atari %s %s\n",
			stone2str(m->color), coord2sstr(m->coord, b));
	return !selfatari;
}


struct playout_policy *
playout_moggy_init(char *arg, struct board *b)
{
	struct playout_policy *p = calloc2(1, sizeof(*p));
	struct moggy_policy *pp = calloc2(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_moggy_choose;
	p->assess = playout_moggy_assess;
	p->permit = playout_moggy_permit;

	int rate = 90;

	pp->lcapturerate = pp->atarirate = pp->capturerate = pp->patternrate = pp->selfatarirate
			= -1;
	pp->korate = 0; pp->koage = 4;
	pp->alwaysccaprate = 0;
	pp->ladders = pp->borderladders = true;
	pp->ladderassess = true;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "lcapturerate") && optval) {
				pp->lcapturerate = atoi(optval);
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				pp->atarirate = atoi(optval);
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "selfatarirate") && optval) {
				pp->selfatarirate = atoi(optval);
			} else if (!strcasecmp(optname, "korate") && optval) {
				pp->korate = atoi(optval);
			} else if (!strcasecmp(optname, "alwaysccaprate") && optval) {
				pp->alwaysccaprate = atoi(optval);
			} else if (!strcasecmp(optname, "rate") && optval) {
				rate = atoi(optval);
			} else if (!strcasecmp(optname, "fillboardtries")) {
				pp->fillboardtries = atoi(optval);
			} else if (!strcasecmp(optname, "koage") && optval) {
				pp->koage = atoi(optval);
			} else if (!strcasecmp(optname, "ladders")) {
				pp->ladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "borderladders")) {
				pp->borderladders = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "ladderassess")) {
				pp->ladderassess = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "assess_local")) {
				pp->assess_local = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "pattern2")) {
				pp->pattern2 = optval && *optval == '0' ? false : true;
			} else {
				fprintf(stderr, "playout-moggy: Invalid policy argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}
	if (pp->lcapturerate == -1) pp->lcapturerate = rate;
	if (pp->atarirate == -1) pp->atarirate = rate;
	if (pp->capturerate == -1) pp->capturerate = rate;
	if (pp->patternrate == -1) pp->patternrate = rate;
	if (pp->selfatarirate == -1) pp->selfatarirate = rate;
	if (pp->korate == -1) pp->korate = rate;
	if (pp->alwaysccaprate == -1) pp->alwaysccaprate = rate;

	pattern3s_init(&pp->patterns, moggy_patterns_src, moggy_patterns_src_n);

	return p;
}
