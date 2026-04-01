#ifndef PACHI_TACTICS_SELFATARI_H
#define PACHI_TACTICS_SELFATARI_H

/* A fairly reliable self-atari detector. */

#include "board.h"
#include "board_undo.h"
#include "debug.h"

typedef struct {
	int     groupcts[S_MAX];	/* Number of neighbor groups for each color */
	group_t groupids[S_MAX][4];	/* And their ids */
	coord_t lib;			/* Selfatari immediate liberty (if any). */
	coord_t extra_lib;		/* Extra liberty provided by own neighbor group. */
	int     captures;               /* Neighbor coords captured (0-4) */
	group_t snapback_group;		/* If snapback, snapbacked group found. */
} selfatari_state_t;


/* Check if this move is undesirable self-atari (resulting group would have
 * only single liberty and not capture anything; ko is allowed); we mostly
 * want to avoid these moves. The function actually does a rather elaborate
 * tactical check, allowing self-atari moves that are nakade, eye falsification
 * or throw-ins. */
static bool is_bad_selfatari(board_t *b, enum stone color, coord_t to);

/* Check if this move is a really bad self-atari, allowing opponent to capture
 * 3 stones or more that could have been saved / don't look like useful nakade.
 * Doesn't care much about 1 stone / 2 stones business unlike is_bad_selfatari(). */
static bool is_really_bad_selfatari(board_t *b, enum stone color, coord_t to);

/* Check if move results in self-atari. */
static bool is_selfatari(board_t *b, enum stone color, coord_t to);

/* Check if move sets up a snapback.
 * faster than with_move(selfatari) + capturing_group_is_snapback() for checking
 * a potential move. Only checks local situation (doesn't check if snapbacked
 * group has countercaptures).
 * Stores snapbacked group found in @snapback_group if non NULL. */
bool is_snapback(board_t *b, enum stone color, coord_t to, group_t *snapback_group);

/* For testing purposes mostly.
 * Only does the 3lib suicide check of is_bad_selfatari(). */
bool is_3lib_selfatari(board_t *b, enum stone color, coord_t to);

/* Move (color, coord) is a selfatari. Pick an alternative move:
 * - Take away opponent outside liberty if possible (approach from the other side)
 * - Otherwise play approach move. */
coord_t selfatari_cousin(board_t *b, enum stone color, coord_t coord);

/* Same as selfatari_cousin() but only consider approach moves.
 * @bygroup: own 2-lib group on which approach move is played. */
coord_t selfatari_cousin_approach_moves(board_t *b, enum stone color, coord_t coord, group_t *bygroup);


#define SELFATARI_3LIB_SUICIDE		1
#define SELFATARI_BIG_GROUPS_ONLY	2

bool is_bad_selfatari_slow(board_t *b, enum stone color, coord_t to, int flags);

static inline bool
is_bad_selfatari(board_t *b, enum stone color, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == S_NONE);
#endif
	/* More than one immediate liberty, thumbs up! */
	if (immediate_liberty_count(b, to) > 1)
		return false;

	return is_bad_selfatari_slow(b, color, to, SELFATARI_3LIB_SUICIDE);
}

static inline bool
is_really_bad_selfatari(board_t *b, enum stone color, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == S_NONE);
#endif
	/* More than one immediate liberty, thumbs up! */
	if (immediate_liberty_count(b, to) > 1)
		return false;

	return is_bad_selfatari_slow(b, color, to, SELFATARI_BIG_GROUPS_ONLY);
}

static inline bool
is_selfatari(board_t *b, enum stone color, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == S_NONE);
#endif
        /* More than one immediate liberty, thumbs up! */
        if (immediate_liberty_count(b, to) > 1)
                return false;

        bool r = true;
        with_move(b, to, color, {
                group_t g = group_at(b, to);
                if (g && group_libs(b, g) > 1)
                        r = false;
        });

        return r;
}


#endif
