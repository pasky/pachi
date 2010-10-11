#ifndef ZZGO_TACTICS_1LIB_H
#define ZZGO_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "board.h"
#include "debug.h"

struct move_queue;

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(unsigned int alwaysccaprate, struct board *b, group_t group, enum stone to_play,
                       struct move_queue *q, coord_t *ladder, int tag);

#endif
