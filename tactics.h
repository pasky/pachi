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
