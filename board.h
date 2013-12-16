/* probdist.h must be included before the include goard since we require
 * proper including order. */
#include "probdist.h"

#ifndef PACHI_BOARD_H
#define PACHI_BOARD_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "util.h"
#include "stone.h"
#include "move.h"

struct fbook;


/* Maximum supported board size. (Without the S_OFFBOARD edges.) */
#define BOARD_MAX_SIZE 19


/* The board implementation has bunch of optional features.
 * Turn them on below: */

#define WANT_BOARD_C // capturable groups queue

//#define BOARD_SIZE 9 // constant board size, allows better optimization

//#define BOARD_SPATHASH // incremental patternsp.h hashes
#define BOARD_SPATHASH_MAXD 3 // maximal diameter

#define BOARD_PAT3 // incremental 3x3 pattern codes

//#define BOARD_TRAITS 1 // incremental point traits (see struct btraits)
//#define BOARD_TRAIT_SAFE 1 // include btraits.safe (rather expensive, unused)
//#define BOARD_TRAIT_SAFE 2 // include btraits.safe based on full is_bad_selfatari()


#define BOARD_MAX_MOVES (BOARD_MAX_SIZE * BOARD_MAX_SIZE)
#define BOARD_MAX_GROUPS (BOARD_MAX_SIZE * BOARD_MAX_SIZE / 2)


/* Some engines might normalize their reading and skip symmetrical
 * moves. We will tell them how can they do it. */
struct board_symmetry {
	/* Playground is in this rectangle. */
	int x1, x2, y1, y2;
	/* d ==  0: Full rectangle
	 * d ==  1: Top triangle */
	int d;
	/* General symmetry type. */
	/* Note that the above is redundant to this, but just provided
	 * for easier usage. */
	enum {
		SYM_FULL,
		SYM_DIAG_UP,
		SYM_DIAG_DOWN,
		SYM_HORIZ,
		SYM_VERT,
		SYM_NONE
	} type;
};


typedef uint64_t hash_t;
#define PRIhash PRIx64

/* XXX: This really belongs in pattern3.h, unfortunately that would mean
 * a dependency hell. */
typedef uint32_t hash3_t; // 3x3 pattern hash


/* Note that "group" is only chain of stones that is solidly
 * connected for us. */
typedef coord_t group_t;

struct group {
	/* We keep track of only up to GROUP_KEEP_LIBS; over that, we
	 * don't care. */
	/* _Combination_ of these two values can make some difference
	 * in performance - fine-tune. */
#define GROUP_KEEP_LIBS 10
	// refill lib[] only when we hit this; this must be at least 2!
	// Moggy requires at least 3 - see below for semantic impact.
#define GROUP_REFILL_LIBS 5
	coord_t lib[GROUP_KEEP_LIBS];
	/* libs is only LOWER BOUND for the number of real liberties!!!
	 * It denotes only number of items in lib[], thus you can rely
	 * on it to store real liberties only up to <= GROUP_REFILL_LIBS. */
	int libs;
};

struct neighbor_colors {
	char colors[S_MAX];
};


/* Point traits bitmap; we update this information incrementally,
 * it can be used e.g. for fast pattern features matching. */
struct btraits {
	/* Number of neighbors we can capture. 0=this move is
	 * not capturing, 1..4=this many neighbors we can capture
	 * (can be multiple neighbors of same group). */
	unsigned cap:3;
	/* Number of 1-stone neighbors we can capture. */
	unsigned cap1:3;
#ifdef BOARD_TRAIT_SAFE
	/* Whether it is SAFE to play here. This is essentially just
	 * cached result of board_safe_to_play(). (Of course the concept
	 * of "safety" is not perfect here, but it's the cheapest
	 * reasonable thing we can do.) */
	bool safe:1;
#endif
	/* Whether we need to re-compute this coordinate; used to
	 * weed out duplicates. Maintained only for S_BLACK. */
	bool dirty:1;
};


/* You should treat this struct as read-only. Always call functions below if
 * you want to change it. */

struct board {
	int size; /* Including S_OFFBOARD margin - see below. */
	int size2; /* size^2 */
	int bits2; /* ceiling(log2(size2)) */
	int captures[S_MAX];
	floating_t komi;
	int handicap;
	/* The ruleset is currently almost never taken into account;
	 * the board implementation is basically Chinese rules (handicap
	 * stones compensation) w/ suicide (or you can look at it as
	 * New Zealand w/o handi stones compensation), while the engine
	 * enforces no-suicide, making for real Chinese rules.
	 * However, we accept suicide moves by the opponent, so we
	 * should work with rules allowing suicide, just not taking
	 * full advantage of them. */
	enum go_ruleset {
		RULES_CHINESE, /* default value */
		RULES_AGA,
		RULES_NEW_ZEALAND,
		RULES_JAPANESE,
		RULES_STONES_ONLY, /* do not count eyes */
		/* http://home.snafu.de/jasiek/siming.html */
		/* Simplified ING rules - RULES_CHINESE with handicaps
		 * counting as points and pass stones. Also should
		 * allow suicide, but Pachi will never suicide
		 * nevertheless. */
		/* XXX: I couldn't find the point about pass stones
		 * in the rule text, but it is Robert Jasiek's
		 * interpretation of them... These rules were
		 * used e.g. at the EGC2012 13x13 tournament.
		 * They are not supported by KGS. */
		RULES_SIMING,
	} rules;

	char *fbookfile;
	struct fbook *fbook;

	/* Iterator offsets for foreach_neighbor*() */
	int nei8[8], dnei[4];

	int moves;
	struct move last_move;
	struct move last_move2; /* second-to-last move */
	struct move last_move3; /* just before last_move2, only set if last_move is pass */
	struct move last_move4; /* just before last_move3, only set if last_move & last_move2 are pass */
	/* Whether we tried to add a hash twice; board_play*() can
	 * set this, but it will still carry out the move as well! */
	bool superko_violation;

	/* The following two structures are goban maps and are indexed by
	 * coord.pos. The map is surrounded by a one-point margin from
	 * S_OFFBOARD stones in order to speed up some internal loops.
	 * Some of the foreach iterators below might include these points;
	 * you need to handle them yourselves, if you need to. */

	/* Stones played on the board */
	enum stone *b; /* enum stone */
	/* Group id the stones are part of; 0 == no group */
	group_t *g;
	/* Positions of next stones in the stone group; 0 == last stone */
	coord_t *p;
	/* Neighboring colors; numbers of neighbors of index color */
	struct neighbor_colors *n;
	/* Zobrist hash for each position */
	hash_t *h;
#ifdef BOARD_SPATHASH
	/* For spatial hashes, we use only 24 bits. */
	/* [0] is d==1, we don't keep hash for d==0. */
	/* We keep hashes for black-to-play ([][0]) and white-to-play
	 * ([][1], reversed stone colors since we match all patterns as
	 * black-to-play). */
	uint32_t (*spathash)[BOARD_SPATHASH_MAXD][2];
#endif
#ifdef BOARD_PAT3
	/* 3x3 pattern code for each position; see pattern3.h for encoding
	 * specification. The information is only valid for empty points. */
	hash3_t *pat3;
#endif
#ifdef BOARD_TRAITS
	/* Incrementally matched point traits information, black-to-play
	 * ([][0]) and white-to-play ([][1]). */
	/* The information is only valid for empty points. */
	struct btraits (*t)[2];
#endif
	/* Cached information on x-y coordinates so that we avoid division. */
	uint8_t (*coord)[2];

	/* Group information - indexed by gid (which is coord of base group stone) */
	struct group *gi;

	/* Positions of free positions - queue (not map) */
	/* Note that free position here is any valid move; including single-point eyes!
	 * However, pass is not included. */
	coord_t *f; int flen;

#ifdef WANT_BOARD_C
	/* Queue of capturable groups */
	group_t *c; int clen;
#endif

#ifdef BOARD_TRAITS
	/* Queue of positions that need their traits updated */
	coord_t *tq; int tqlen;
#endif

	/* Symmetry information */
	struct board_symmetry symmetry;

	/* Last ko played on the board. */
	struct move last_ko;
	int last_ko_age;

	/* Basic ko check */
	struct move ko;

	/* Engine-specific state; persistent through board development,
	 * is reset only at clear_board. */
	void *es;

	/* Playout-specific state; persistent through board development,
	 * but its lifetime is maintained in play_random_game(); it should
	 * not be set outside of it. */
	void *ps;


	/* --- PRIVATE DATA --- */

	/* For superko check: */

	/* Board "history" - hashes encountered. Size of the hash should be
	 * >> board_size^2. */
#define history_hash_bits 12
#define history_hash_mask ((1 << history_hash_bits) - 1)
#define history_hash_prev(i) ((i - 1) & history_hash_mask)
#define history_hash_next(i) ((i + 1) & history_hash_mask)
	hash_t history_hash[1 << history_hash_bits];
	/* Hash of current board position. */
	hash_t hash;
	/* Hash of current board position quadrants. */
	hash_t qhash[4];
};

#ifdef BOARD_SIZE
/* Avoid unused variable warnings */
#define board_size(b_) (((b_) == (b_)) ? BOARD_SIZE + 2 : 0)
#define board_size2(b_) (board_size(b_) * board_size(b_))
#else
#define board_size(b_) ((b_)->size)
#define board_size2(b_) ((b_)->size2)
#endif

/* This is a shortcut for taking different action on smaller
 * and large boards (e.g. picking different variable defaults).
 * This is of course less optimal than fine-tuning dependency
 * function of values on board size, but that is difficult and
 * possibly not very rewarding if you are interested just in
 * 9x9 and 19x19. */
#define board_large(b_) (board_size(b_)-2 >= 15)
#define board_small(b_) (board_size(b_)-2 <= 9)

#if BOARD_SIZE == 19
#  define board_bits2(b_) 9
#elif BOARD_SIZE == 13
#  define board_bits2(b_) 8
#elif BOARD_SIZE == 9
#  define board_bits2(b_) 7
#else
#  define board_bits2(b_) ((b_)->bits2)
#endif

#define board_at(b_, c) ((b_)->b[c])
#define board_atxy(b_, x, y) ((b_)->b[(x) + board_size(b_) * (y)])

#define group_at(b_, c) ((b_)->g[c])
#define group_atxy(b_, x, y) ((b_)->g[(x) + board_size(b_) * (y)])

/* Warning! Neighbor count is not kept up-to-date for S_NONE! */
#define neighbor_count_at(b_, coord, color) ((b_)->n[coord].colors[(enum stone) color])
#define set_neighbor_count_at(b_, coord, color, count) (neighbor_count_at(b_, coord, color) = (count))
#define inc_neighbor_count_at(b_, coord, color) (neighbor_count_at(b_, coord, color)++)
#define dec_neighbor_count_at(b_, coord, color) (neighbor_count_at(b_, coord, color)--)
#define immediate_liberty_count(b_, coord) (4 - neighbor_count_at(b_, coord, S_BLACK) - neighbor_count_at(b_, coord, S_WHITE) - neighbor_count_at(b_, coord, S_OFFBOARD))

#define trait_at(b_, coord, color) (b_)->t[coord][(color) - 1]

#define groupnext_at(b_, c) ((b_)->p[c])
#define groupnext_atxy(b_, x, y) ((b_)->p[(x) + board_size(b_) * (y)])

#define group_base(g_) (g_)
#define group_is_onestone(b_, g_) (groupnext_at(b_, group_base(g_)) == 0)
#define board_group_info(b_, g_) ((b_)->gi[(g_)])
#define board_group_captured(b_, g_) (board_group_info(b_, g_).libs == 0)
/* board_group_other_lib() makes sense only for groups with two liberties. */
#define board_group_other_lib(b_, g_, l_) (board_group_info(b_, g_).lib[board_group_info(b_, g_).lib[0] != (l_) ? 0 : 1])

#define hash_at(b_, coord, color) ((b_)->h[((color) == S_BLACK ? board_size2(b_) : 0) + coord])

struct board *board_init(char *fbookfile);
struct board *board_copy(struct board *board2, struct board *board1);
void board_done_noalloc(struct board *board);
void board_done(struct board *board);
/* size here is without the S_OFFBOARD margin. */
void board_resize(struct board *board, int size);
void board_clear(struct board *board);

struct FILE;
typedef char *(*board_cprint)(struct board *b, coord_t c, char *s, char *end);
void board_print(struct board *board, FILE *f);
void board_print_custom(struct board *board, FILE *f, board_cprint cprint);

/* Place given handicap on the board; coordinates are printed to f. */
void board_handicap(struct board *board, int stones, FILE *f);

/* Returns group id, 0 on allowed suicide, pass or resign, -1 on error */
int board_play(struct board *board, struct move *m);
/* Like above, but plays random move; the move coordinate is recorded
 * to *coord. This method will never fill your own eye. pass is played
 * when no move can be played. You can impose extra restrictions if you
 * supply your own permit function; the permit function can also modify
 * the move coordinate to redirect the move elsewhere. */
typedef bool (*ppr_permit)(void *data, struct board *b, struct move *m);
void board_play_random(struct board *b, enum stone color, coord_t *coord, ppr_permit permit, void *permit_data);

/*Undo, supported only for pass moves. Returns -1 on error, 0 otherwise. */
int board_undo(struct board *board);

/* Returns true if given move can be played. */
static bool board_is_valid_play(struct board *b, enum stone color, coord_t coord);
static bool board_is_valid_move(struct board *b, struct move *m);
/* Returns true if ko was just taken. */
static bool board_playing_ko_threat(struct board *b);
/* Returns 0 or ID of neighboring group in atari. */
static group_t board_get_atari_neighbor(struct board *b, coord_t coord, enum stone group_color);
/* Returns true if the move is not obvious self-atari. */
static bool board_safe_to_play(struct board *b, coord_t coord, enum stone color);

/* Determine number of stones in a group, up to @max stones. */
static int group_stone_count(struct board *b, group_t group, int max);

/* Adjust symmetry information as if given coordinate has been played. */
void board_symmetry_update(struct board *b, struct board_symmetry *symmetry, coord_t c);
/* Check if coordinates are within symmetry base. (If false, they can
 * be derived from the base.) */
static bool board_coord_in_symmetry(struct board *b, coord_t c);

/* Returns true if given coordinate has all neighbors of given color or the edge. */
static bool board_is_eyelike(struct board *board, coord_t coord, enum stone eye_color);
/* Returns true if given coordinate could be a false eye; this check makes
 * sense only if you already know the coordinate is_eyelike(). */
bool board_is_false_eyelike(struct board *board, coord_t coord, enum stone eye_color);
/* Returns true if given coordinate is a 1-pt eye (checks against false eyes, or
 * at least tries to). */
bool board_is_one_point_eye(struct board *board, coord_t c, enum stone eye_color);
/* Returns color of a 1pt eye owner, S_NONE if not an eye. */
enum stone board_get_one_point_eye(struct board *board, coord_t c);

/* board_official_score() is the scoring method for yielding score suitable
 * for external presentation. For fast scoring of entirely filled boards
 * (e.g. playouts), use board_fast_score(). */
/* Positive: W wins */
/* Compare number of stones + 1pt eyes. */
floating_t board_fast_score(struct board *board);
/* Tromp-Taylor scoring, assuming given groups are actually dead. */
struct move_queue;
floating_t board_official_score(struct board *board, struct move_queue *mq);

/* Set board rules according to given string. Returns false in case
 * of unknown ruleset name. */
bool board_set_rules(struct board *board, char *name);

/** Iterators */

#define foreach_point(board_) \
	do { \
		coord_t c = 0; \
		for (; c < board_size(board_) * board_size(board_); c++)
#define foreach_point_and_pass(board_) \
	do { \
		coord_t c = pass; \
		for (; c < board_size(board_) * board_size(board_); c++)
#define foreach_point_end \
	} while (0)

#define foreach_free_point(board_) \
	do { \
		int fmax__ = (board_)->flen; \
		for (int f__ = 0; f__ < fmax__; f__++) { \
			coord_t c = (board_)->f[f__];
#define foreach_free_point_end \
		} \
	} while (0)

#define foreach_in_group(board_, group_) \
	do { \
		struct board *board__ = board_; \
		coord_t c = group_base(group_); \
		coord_t c2 = c; c2 = groupnext_at(board__, c2); \
		do {
#define foreach_in_group_end \
			c = c2; c2 = groupnext_at(board__, c2); \
		} while (c != 0); \
	} while (0)

/* NOT VALID inside of foreach_point() or another foreach_neighbor(), or rather
 * on S_OFFBOARD coordinates. */
#define foreach_neighbor(board_, coord_, loop_body) \
	do { \
		struct board *board__ = board_; \
		coord_t coord__ = coord_; \
		coord_t c; \
		c = coord__ - board_size(board__); do { loop_body } while (0); \
		c = coord__ - 1; do { loop_body } while (0); \
		c = coord__ + 1; do { loop_body } while (0); \
		c = coord__ + board_size(board__); do { loop_body } while (0); \
	} while (0)

#define foreach_8neighbor(board_, coord_) \
	do { \
		int fn__i; \
		coord_t c = (coord_); \
		for (fn__i = 0; fn__i < 8; fn__i++) { \
			c += (board_)->nei8[fn__i];
#define foreach_8neighbor_end \
		} \
	} while (0)

#define foreach_diag_neighbor(board_, coord_) \
	do { \
		int fn__i; \
		coord_t c = (coord_); \
		for (fn__i = 0; fn__i < 4; fn__i++) { \
			c += (board_)->dnei[fn__i];
#define foreach_diag_neighbor_end \
		} \
	} while (0)


static inline bool
board_is_eyelike(struct board *board, coord_t coord, enum stone eye_color)
{
	return (neighbor_count_at(board, coord, eye_color)
	        + neighbor_count_at(board, coord, S_OFFBOARD)) == 4;
}

static inline bool
board_is_valid_play(struct board *board, enum stone color, coord_t coord)
{
	if (board_at(board, coord) != S_NONE)
		return false;
	if (!board_is_eyelike(board, coord, stone_other(color)))
		return true;
	/* Play within {true,false} eye-ish formation */
	if (board->ko.coord == coord && board->ko.color == color)
		return false;
#ifdef BOARD_TRAITS
	/* XXX: Disallows suicide. */
	return trait_at(board, coord, color).cap > 0;
#else
	int groups_in_atari = 0;
	foreach_neighbor(board, coord, {
		group_t g = group_at(board, c);
		groups_in_atari += (board_group_info(board, g).libs == 1);
	});
	return !!groups_in_atari;
#endif
}

static inline bool
board_is_valid_move(struct board *board, struct move *m)
{
	return board_is_valid_play(board, m->color, m->coord);
}

static inline bool
board_playing_ko_threat(struct board *b)
{
	return !is_pass(b->ko.coord);
}

static inline group_t
board_get_atari_neighbor(struct board *b, coord_t coord, enum stone group_color)
{
#ifdef BOARD_TRAITS
	if (!trait_at(b, coord, stone_other(group_color)).cap) return 0;
#endif
	foreach_neighbor(b, coord, {
		group_t g = group_at(b, c);
		if (g && board_at(b, c) == group_color && board_group_info(b, g).libs == 1)
			return g;
		/* We return first match. */
	});
	return 0;
}

static inline bool
board_safe_to_play(struct board *b, coord_t coord, enum stone color)
{
	/* number of free neighbors */
	int libs = immediate_liberty_count(b, coord);
	if (libs > 1)
		return true;

#ifdef BOARD_TRAITS
	/* number of capturable enemy groups */
	if (trait_at(b, coord, color).cap > 0)
		return true; // XXX: We don't account for snapback.
	/* number of non-capturable friendly groups */
	int noncap_ours = neighbor_count_at(b, coord, color) - trait_at(b, coord, stone_other(color)).cap;
	if (noncap_ours < 1)
		return false;
/*#else see below */
#endif

	/* ok, but we need to check if they don't have just two libs. */
	coord_t onelib = -1;
	foreach_neighbor(b, coord, {
#ifndef BOARD_TRAITS
		if (board_at(b, c) == stone_other(color) && board_group_info(b, group_at(b, c)).libs == 1)
			return true; // can capture; no snapback check
#endif
		if (board_at(b, c) != color) continue;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs == 1) continue; // in atari
		if (board_group_info(b, g).libs == 2) { // two liberties
			if (libs > 0) return true; // we already have one real liberty
			/* we might be connecting two 2-lib groups, which is ok;
			 * so remember the other liberty and just make sure it's
			 * not the same one */
			if (onelib >= 0 && c != onelib) return true;
			onelib = board_group_other_lib(b, g, c);
			continue;
		}
		// many liberties
		return true;
	});
	// no good support group
	return false;
}

static inline int
group_stone_count(struct board *b, group_t group, int max)
{
	int n = 0;
	foreach_in_group(b, group) {
		n++;
		if (n >= max) return max;
	} foreach_in_group_end;
	return n;
}

static inline bool
board_coord_in_symmetry(struct board *b, coord_t c)
{
	if (coord_y(c, b) < b->symmetry.y1 || coord_y(c, b) > b->symmetry.y2)
		return false;
	if (coord_x(c, b) < b->symmetry.x1 || coord_x(c, b) > b->symmetry.x2)
		return false;
	if (b->symmetry.d) {
		int x = coord_x(c, b);
		if (b->symmetry.type == SYM_DIAG_DOWN)
			x = board_size(b) - 1 - x;
		if (x > coord_y(c, b))
			return false;
	}
	return true;

}

#endif
