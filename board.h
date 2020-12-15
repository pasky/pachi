#ifndef PACHI_BOARD_H
#define PACHI_BOARD_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "stone.h"
#include "move.h"
#include "mq.h"

struct ownermap;


/**************************************************************************************/
/* The board implementation has bunch of optional features: */

#define WANT_BOARD_C              /* Capturable groups queue */

//#define BOARD_SIZE 9            /* Fixed board size, allows better optimization */

//#define BOARD_PAT3              /* Incremental 3x3 pattern codes */
                                  /* XXX faster without ?! */

//#define BOARD_HASH_COMPAT	  /* Enable to get same hashes as old Pachi versions. */

//#define BOARD_UNDO_CHECKS 1     /* Guard against invalid quick_play() / quick_undo() uses */

#define BOARD_LAST_N 4            /* Previous moves. */

#define BOARD_HASH_HISTORY 16

/**************************************************************************************/

/* Maximum supported board size. (Without the S_OFFBOARD edges.) */
#define BOARD_MAX_SIZE 19

#define BOARD_MAX_COORDS  ((BOARD_MAX_SIZE+2) * (BOARD_MAX_SIZE+2))
#define BOARD_MAX_MOVES   (BOARD_MAX_SIZE * BOARD_MAX_SIZE)
#define BOARD_MAX_GROUPS  (BOARD_MAX_SIZE * BOARD_MAX_SIZE * 2 / 3)
/* For 19x19, max 19*2*6 = 228 groups (stacking b&w stones, each third line empty) */


typedef uint64_t hash_t;
#define PRIhash PRIx64

                             /* XXX This really belongs in pattern3.h, unfortunately that would mean a dependency hell. */
typedef uint32_t hash3_t;    /* 3x3 pattern hash */

typedef coord_t group_t;     /* Note that "group" is only chain of stones that is solidly connected for us. */


typedef struct {              /* Keep track of only up to GROUP_KEEP_LIBS. over that, we don't care. */
#define GROUP_KEEP_LIBS  10   /* _Combination_ of these two values can make some difference in performance. */
#define GROUP_REFILL_LIBS 5   /* Refill lib[] only when we hit this; this must be at least 2!
			       * Moggy requires at least 3 - see below for semantic impact. */
	int libs;             /* libs is only LOWER BOUND for the number of real liberties!!!	
			       * It denotes only number of items in lib[], thus you can rely
			       * on it to store real liberties only up to <= GROUP_REFILL_LIBS. */
	coord_t lib[GROUP_KEEP_LIBS];  
} group_info_t;


typedef struct {
	char colors[S_MAX];
} neighbors_t;


/* Quick hack to help ensure tactics code stays within quick board limitations.
 * Ideally we'd have two different types for boards and quick_boards. The idea
 * of having casts / duplicate api all over the place isn't so appealing though... */
#ifndef QUICK_BOARD_CODE
#define FB_ONLY(field)  field
#else
#define FB_ONLY(field)  field ## _disabled
// Try to make error messages more helpful ...
#define clen clen_field_not_supported_for_quick_boards
#define flen flen_field_not_supported_for_quick_boards
#endif

/* The ruleset is currently almost never taken into account;
 * the board implementation is basically Chinese rules (handicap
 * stones compensation) w/ suicide (or you can look at it as
 * New Zealand w/o handi stones compensation), while the engine
 * enforces no-suicide, making for real Chinese rules.
 * However, we accept suicide moves by the opponent, so we
 * should work with rules allowing suicide, just not taking
 * full advantage of them. */
enum rules {
	RULES_INVALID,
	RULES_CHINESE,        /* default */
	RULES_AGA,
	RULES_NEW_ZEALAND,
	RULES_JAPANESE,
	RULES_STONES_ONLY,    /* do not count eyes */	
	
	/* Simplified ING rules - RULES_CHINESE with handicaps counting as points and pass stones.
	 * http://home.snafu.de/jasiek/siming.html
	 * Also should allow suicide, but Pachi will never suicide nevertheless.
	 * XXX: I couldn't find the point about pass stones in the rule text, but it is Robert Jasiek's
	 *      interpretation of them... These rules were used e.g. at the EGC2012 13x13 tournament.
	 *      They are not supported by KGS. */
	RULES_SIMING,
};

/* Data shared by all boards of a given size */
typedef struct {
	int rsize;                          /* real board size     (19x19: 19) */
	int stride;                         /* padded board size   (19x19: 21) */
	int max_coords;                     /* stride^2 */
	int bits2;                          /* ceiling(log2(size2)) */
	
	int nei8[8], dnei[4];               /* Iterator offsets for foreach_neighbor*() */
	
	hash_t h[BOARD_MAX_COORDS][2];      /* Fixed zobrist hashes for all coords (black and white) */
	
	uint8_t coord[BOARD_MAX_COORDS][2]; /* Cached x-y coord info so we avoid division. */
} board_statics_t;

/* Only one board size in use at any given time so don't need array */
extern board_statics_t board_statics;


/* You should treat this struct as read-only.
 * Always call functions below if you want to change it. */

typedef struct board {
	int rsize;                 /* Real board size   (19x19: 19) */

	int moves;
	int captures[S_MAX];
	int passes[S_MAX];
	floating_t komi;
	int handicap;
	enum rules rules;

	move_t last_moves[BOARD_LAST_N]; /* Last moves (circular buffer) */
	int    last_move_i;              /* For quick boards only last two moves are maintained. */

	move_t ko;                /* Basic ko check */	
	move_t last_ko;           /* Last ko played on the board. */
	int    last_ko_age;

FB_ONLY(bool superko_violation);  /* Whether we tried to add a hash twice; board_play*() can
				   * set this, but it will still carry out the move as well! */

	/* The following structures are goban maps and are indexed by coord. The map 
	 * is surrounded by a one-point margin from S_OFFBOARD stones in order to
	 * speed up some internal loops. Some of the foreach iterators below might
	 * include these points; you need to handle them yourselves, if you need to. */	
	
	enum stone b[BOARD_MAX_COORDS];    /* Stones played on the board */
	neighbors_t n[BOARD_MAX_COORDS];   /* Neighboring colors; numbers of neighbors of index color */
	
	group_t g[BOARD_MAX_COORDS];       /* Group id the stones are part of; 0 == no group */	
	group_info_t gi[BOARD_MAX_COORDS]; /* Group information - indexed by gid (which is coord of base group stone) */
	coord_t p[BOARD_MAX_COORDS];       /* Positions of next stones in the stone group; 0 == last stone */

#ifdef BOARD_PAT3       
FB_ONLY(hash3_t pat3)[BOARD_MAX_COORDS];   /* 3x3 pattern hash for each position; see pattern3.h for encoding
					    * specification. The information is only valid for empty points. */
#endif

FB_ONLY(coord_t f)[BOARD_MAX_COORDS];      /* List of free positions - free position here is any valid move */
FB_ONLY(int flen);                         /* including single-point eyes! */
FB_ONLY(int fmap)[BOARD_MAX_COORDS];       /* Map free positions coords to their list index, for quick lookup. */

#ifdef WANT_BOARD_C	
FB_ONLY(group_t c)[BOARD_MAX_GROUPS];      /* List of capturable groups */
FB_ONLY(int clen);
#endif

FB_ONLY(bool playout_board);

/*************************************************************************************************************/
/* Not maintained during playouts: */
	
FB_ONLY(hash_t hash);                             /* Hash of current board position. */
FB_ONLY(hash_t hash_history)[BOARD_HASH_HISTORY]; /* Last hashes encountered, for superko check. */
	int    hash_history_next;                 /* (circular buffer) */


/*************************************************************************************************************/

#ifdef DCNN_DARKFOREST
FB_ONLY(int moveno)[BOARD_MAX_COORDS];     /* Move number for each coord */
#endif

#ifdef BOARD_UNDO_CHECKS	
	int quicked;                       /* Guard against invalid quick_play() / quick_undo() uses */
#endif
	
	struct board_undo *u;              /* For quick_play() */

	char *fbookfile;
	struct fbook *fbook;		   /* Opening book */
	 
	void *ps;                          /* Playout-specific state; persistent through board development,
					    * initialized by play_random_game() and free()'d at board destroy time */
} board_t;


#ifdef BOARD_SIZE
#define board_rsize(b)          BOARD_SIZE
#define the_board_rsize()       BOARD_SIZE
#define the_board_stride()        (BOARD_SIZE + 2)
#define board_max_coords(b)       (board_stride(b) * board_stride(b))
#else
#define board_rsize(b)          ((b)->rsize)
#define the_board_rsize()       (board_statics.rsize)
#define the_board_stride()        (board_statics.stride)
#define board_max_coords(b)       (board_statics.max_coords)
#endif

#define board_stride(b)           (board_rsize(b) + 2)
#define board_rsize2(b)		  (board_rsize(b) * board_rsize(b))


/* This is a shortcut for taking different action on smaller and large boards 
 * (e.g. picking different variable defaults). This is of course less optimal than
 * fine-tuning dependency function of values on board size, but that is difficult
 * and possibly not very rewarding if you are interested just in 9x9 and 19x19. */
#define board_large(b_) (board_rsize(b_) >= 15)
#define board_small(b_) (board_rsize(b_) <= 9)

#if BOARD_SIZE == 19
#  define board_bits2() 9
#elif BOARD_SIZE == 13
#  define board_bits2() 8
#elif BOARD_SIZE == 9
#  define board_bits2() 7
#else
#  define board_bits2() (board_statics.bits2)
#endif

#define last_move(b)  ((b)->last_moves[b->last_move_i])
#define last_move2(b) ((b)->last_moves[(BOARD_LAST_N + b->last_move_i - 1) % BOARD_LAST_N])
#define last_move3(b) ((b)->last_moves[(BOARD_LAST_N + b->last_move_i - 2) % BOARD_LAST_N])
#define last_move4(b) ((b)->last_moves[(BOARD_LAST_N + b->last_move_i - 3) % BOARD_LAST_N])
#define last_move_nexti(b)     (((b)->last_move_i + 1) % BOARD_LAST_N)
#define last_move_previ(b, n)  ((BOARD_LAST_N + b->last_move_i - (n)) % BOARD_LAST_N)
#define last_moven(b, n) ((b)->last_moves[last_move_previ(b, n)])

#define board_at(b_, c)      ((b_)->b[c])
#define board_atxy(b_, x, y) ((b_)->b[coord_xy(x, y)])

#define group_at(b_, c)      ((b_)->g[c])
#define group_atxy(b_, x, y) ((b_)->g[coord_xy(x, y)])

/* Warning! Neighbor count is not kept up-to-date for S_NONE! */
#define neighbor_count_at(b_, coord, color) ((b_)->n[coord].colors[(enum stone) color])
#define set_neighbor_count_at(b_, coord, color, count) (neighbor_count_at(b_, coord, color) = (count))
#define inc_neighbor_count_at(b_, coord, color) (neighbor_count_at(b_, coord, color)++)
#define dec_neighbor_count_at(b_, coord, color) (neighbor_count_at(b_, coord, color)--)
#define immediate_liberty_count(b_, coord) (4 - neighbor_count_at(b_, coord, S_BLACK) - neighbor_count_at(b_, coord, S_WHITE) - neighbor_count_at(b_, coord, S_OFFBOARD))

#define groupnext_at(b_, c) ((b_)->p[c])

#define group_base(g_) (g_)
#define group_is_onestone(b_, g_) (groupnext_at(b_, group_base(g_)) == 0)
#define board_group_info(b_, g_) ((b_)->gi[(g_)])
#define board_group_captured(b_, g_) (board_group_info(b_, g_).libs == 0)
/* board_group_other_lib() makes sense only for groups with two liberties. */
#define board_group_other_lib(b_, g_, l_) (board_group_info(b_, g_).lib[board_group_info(b_, g_).lib[0] != (l_) ? 0 : 1])

#ifdef BOARD_HASH_COMPAT
#define hash_at(coord, color) (*(&board_statics.h[0][0] + ((color) == S_BLACK ? board_statics.max_coords : 0) + (coord)))
#else
#define hash_at(coord, color) (board_statics.h[coord][(color) == S_BLACK])
#endif

#define playout_board(b) ((b)->playout_board)

board_t *board_new(int size, char *fbookfile);
void board_delete(board_t **board);
void board_copy(board_t *board2, board_t *board1);
void board_done(board_t *board);

void board_resize(board_t *b, int size);
void board_clear(board_t *board);

typedef void  (*board_cprint)(board_t *b, coord_t c, strbuf_t *buf, void *data);
typedef char *(*board_print_handler)(board_t *b, coord_t c, void *data);
void board_print(board_t *b, FILE *f);
void board_print_custom(board_t *b, FILE *f, board_cprint cprint, void *data);
void board_hprint(board_t *b, FILE *f, board_print_handler handler, void *data);
/* @target_move displayed as '*' (must be empty spot) */
void board_print_target_move(board_t *b, FILE *f, coord_t target_move);

/* Debugging: Compare 2 boards byte by byte. Don't use that for sorting =) */
int board_cmp(board_t *b1, board_t *b2);

/* Place given handicap on the board; coordinates are printed to f. */
void board_handicap(board_t *b, int stones, move_queue_t *q);

/* Return color to play */
static enum stone board_to_play(board_t *b);

/* Returns group id, 0 on allowed suicide, pass or resign, -1 on error */
int board_play(board_t *b, move_t *m);
/* Like above, but plays random move; the move coordinate is recorded
 * to *coord. This method will never fill your own eye. pass is played
 * when no move can be played. You can impose extra restrictions if you
 * supply your own permit function; the permit function can also modify
 * the move coordinate to redirect the move elsewhere. */
typedef bool (*ppr_permit)(board_t *b, move_t *m, void *data);
bool board_permit(board_t *b, move_t *m, void *data);
void board_play_random(board_t *b, enum stone color, coord_t *coord, ppr_permit permit, void *permit_data);

/* Returns true if given move can be played. */
static bool board_is_valid_play(board_t *b, enum stone color, coord_t coord);
static bool board_is_valid_move(board_t *b, move_t *m);
/* Returns true if ko was just taken. */
static bool board_playing_ko_threat(board_t *b);

/* Determine number of stones in a group, up to @max stones. */
static int group_stone_count(board_t *b, group_t group, int max);

/* Returns true if given coordinate has all neighbors of given color or the edge. */
static bool board_is_eyelike(board_t *b, coord_t coord, enum stone eye_color);
/* Returns true if given coordinate could be a false eye; this check makes
 * sense only if you already know the coordinate is_eyelike(). */
bool board_is_false_eyelike(board_t *b, coord_t coord, enum stone eye_color);
/* Returns true if given coordinate is a 1-pt eye (checks against false eyes, or
 * at least tries to). */
bool board_is_one_point_eye(board_t *b, coord_t c, enum stone eye_color);
/* Returns 1pt eye color (can be false-eye) */
enum stone board_eye_color(board_t *board, coord_t c);

/* For final ownermaps (see board_official_score_details()) */
#define FO_DAME  S_NONE

/* Scoring functions:  Positive: W wins */
/* board_official_score() is the scoring method for yielding score suitable
 * for external presentation. For fast scoring of entirely filled boards
 * (e.g. playouts), use board_fast_score(). */
/* Playout scoring: Compare number of stones + 1pt eyes. */
floating_t board_fast_score(board_t *board);
floating_t board_score(board_t *b, int scores[S_MAX]);
/* Tromp-Taylor scoring, assuming given groups are actually dead. */
floating_t board_official_score(board_t *b, move_queue_t *dead);
char*      board_official_score_str(board_t *b, move_queue_t *dead);
floating_t board_official_score_color(board_t *b, move_queue_t *dead, enum stone color);
floating_t board_official_score_details(board_t *b, move_queue_t *dead, int *dame, int *seki, int *ownermap, struct ownermap *po);
void       board_print_official_ownermap(board_t *b, move_queue_t *dead);

/* Set board rules according to given string.
 * Returns false in case of unknown ruleset. */
bool board_set_rules(board_t *b, const char *name);
enum rules board_parse_rules(const char *name);
const char *rules2str(enum rules rules);


/** Iterators */

#define foreach_point(board_) \
	do { \
		coord_t c = 0; \
		for (; c < board_max_coords(board_); c++)
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
		board_t *board__ = board_; \
		for (coord_t c = group_base(group_); c; c = groupnext_at(board__, c))
#define foreach_in_group_end \
	} while (0)

/* NOT VALID inside of foreach_point() or another foreach_neighbor(), or rather
 * on S_OFFBOARD coordinates. */
#define foreach_neighbor(board_, coord_, loop_body) \
	do { \
		coord_t coord__ = coord_; \
		coord_t c; \
		c = coord__ - board_stride(board_); do { loop_body } while (0); \
		c = coord__ - 1; do { loop_body } while (0); \
		c = coord__ + 1; do { loop_body } while (0); \
		c = coord__ + board_stride(board_); do { loop_body } while (0); \
	} while (0)

#define foreach_8neighbor(board_, coord_) \
	do { \
		int fn__i; \
		coord_t c = (coord_); \
		for (fn__i = 0; fn__i < 8; fn__i++) { \
			c += board_statics.nei8[fn__i];
#define foreach_8neighbor_end \
		} \
	} while (0)

#define foreach_diag_neighbor(board_, coord_) \
	do { \
		int fn__i; \
		coord_t c = (coord_); \
		for (fn__i = 0; fn__i < 4; fn__i++) { \
			c += board_statics.dnei[fn__i];
#define foreach_diag_neighbor_end \
		} \
	} while (0)


/* XXX doesn't handle handicap placement phase */
static inline enum stone
board_to_play(board_t *b)
{
	return (last_move(b).color ? stone_other(last_move(b).color) : S_BLACK);
}

static inline bool
board_is_eyelike(board_t *b, coord_t coord, enum stone eye_color)
{
	return (neighbor_count_at(b, coord, eye_color) +
	        neighbor_count_at(b, coord, S_OFFBOARD)) == 4;
}

/* Group suicides allowed */
static inline bool
board_is_valid_play(board_t *b, enum stone color, coord_t coord)
{
	if (board_at(b, coord) != S_NONE)                    return false;
	if (!board_is_eyelike(b, coord, stone_other(color))) return true;
	
	/* Play within {true,false} eye-ish formation */
	if (b->ko.coord == coord && b->ko.color == color) return false;
	int groups_in_atari = 0;
	foreach_neighbor(b, coord, {
		group_t g = group_at(b, c);
		groups_in_atari += (board_group_info(b, g).libs == 1);
	});
	return !!groups_in_atari;
}

/* Check group suicides, slower than board_is_valid_play() */
static inline bool
board_is_valid_play_no_suicide(board_t *b, enum stone color, coord_t coord)
{
	if (board_at(b, coord) != S_NONE)           return false;
	if (immediate_liberty_count(b, coord) >= 1) return true;
	if (board_is_eyelike(b, coord, stone_other(color)) &&
	    b->ko.coord == coord && b->ko.color == color)  return false;

	// Capturing something ?
	foreach_neighbor(b, coord, {
		if (board_at(b, c) == stone_other(color) &&
		    board_group_info(b, group_at(b, c)).libs == 1)
			return true;
	});

	// Neighbour with 2 libs ?
	foreach_neighbor(b, coord, {
		if (board_at(b, c) == color &&
		    board_group_info(b, group_at(b, c)).libs > 1)
			return true;
	});

	return false;  // Suicide
}


static inline bool
board_is_valid_move(board_t *b, move_t *m)
{
	return board_is_valid_play(b, m->color, m->coord);
}

static inline bool
board_playing_ko_threat(board_t *b)
{
	return !is_pass(b->ko.coord);
}


static inline int
group_stone_count(board_t *b, group_t group, int max)
{
	int n = 0;
	foreach_in_group(b, group) {
		n++;
		if (n >= max) return max;
	} foreach_in_group_end;
	return n;
}


#endif
