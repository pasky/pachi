#ifndef ZZGO_TACTICS_H
#define ZZGO_TACTICS_H

/* Advanced tactical checks non-essential to the board implementation. */

#include "board.h"

/* Check if this move is undesirable self-atari (resulting group would have
 * only single liberty and not capture anything; ko is allowed); we mostly
 * want to avoid these moves. The function actually does a rather elaborate
 * tactical check, allowing self-atari moves that are nakade, eye falsification
 * or throw-ins. */
static bool is_bad_selfatari(struct board *b, enum stone color, coord_t to);

/* Checks if there are any stones in n-vincinity of coord. */
bool board_stone_radar(struct board *b, coord_t coord, int distance);

/* Construct a "common fate graph" from given coordinate; that is, a weighted
 * graph of intersections where edges between all neighbors have weight 1,
 * but edges between neighbors of same color have weight 0. Thus, this is
 * "stone chain" metric in a sense. */
/* The output are distanes from start stored in given [board_size2()] array;
 * intersections further away than maxdist have all distance maxdist+1 set. */
void cfg_distances(struct board *b, coord_t start, int *distances, int maxdist);


bool is_bad_selfatari_slow(struct board *b, enum stone color, coord_t to);
static inline bool
is_bad_selfatari(struct board *b, enum stone color, coord_t to)
{
	/* More than one immediate liberty, thumbs up! */
	if (immediate_liberty_count(b, to) > 1)
		return false;

	return is_bad_selfatari_slow(b, color, to);
}

#endif
