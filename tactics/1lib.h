#ifndef PACHI_TACTICS_1LIB_H
#define PACHI_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "board.h"
#include "debug.h"

struct move_queue;


/* Can group @group usefully capture a neighbor ? 
 * (usefully: not a snapback) */
bool can_countercapture(struct board *b, group_t group, struct move_queue *q, int tag);
/* Can group @group capture *any* neighbor ? */
bool can_countercapture_any(struct board *b, group_t group, struct move_queue *q, int tag);

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(unsigned int alwaysccaprate, struct board *b, group_t group, enum stone to_play,
                       struct move_queue *q, coord_t *ladder, bool middle_ladder, int tag);

#endif
