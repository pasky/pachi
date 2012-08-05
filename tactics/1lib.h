#ifndef PACHI_TACTICS_1LIB_H
#define PACHI_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "board.h"
#include "debug.h"

struct move_queue;


/* For given atari group @group owned by @owner, decide if @to_play
 * can save it / keep it in danger by dealing with one of the
 * neighboring groups. */
bool can_countercapture(struct board *b, enum stone owner, group_t g,
		        enum stone to_play, struct move_queue *q, int tag);

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(unsigned int alwaysccaprate, struct board *b, group_t group, enum stone to_play,
                       struct move_queue *q, coord_t *ladder, bool middle_ladder, int tag);

#endif
