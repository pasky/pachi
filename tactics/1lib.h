#ifndef PACHI_TACTICS_1LIB_H
#define PACHI_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "board.h"
#include "debug.h"

struct move_queue;


bool capturing_group_is_snapback(struct board *b, group_t group);
/* Can group @group usefully capture a neighbor ? 
 * (usefully: not a snapback) */
bool can_countercapture(struct board *b, group_t group, struct move_queue *q, int tag);
/* Same as can_countercapture() but returns capturable groups instead of moves,
 * queue may not be NULL, and is always cleared. */
bool countercapturable_groups(struct board *b, group_t group, struct move_queue *q);
/* Can group @group capture *any* neighbor ? */
bool can_countercapture_any(struct board *b, group_t group, struct move_queue *q, int tag);

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(unsigned int alwaysccaprate, struct board *b, group_t group, enum stone to_play,
                       struct move_queue *q, coord_t *ladder, bool middle_ladder, int tag);

#endif
