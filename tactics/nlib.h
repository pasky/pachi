#ifndef PACHI_TACTICS_NLIB_H
#define PACHI_TACTICS_NLIB_H

/* N-liberty semeai defense tactical checks. */

#include "board.h"
#include "debug.h"

struct move_queue;

void group_nlib_defense_check(struct board *b, group_t group, enum stone to_play, struct move_queue *q, int tag);

#endif
