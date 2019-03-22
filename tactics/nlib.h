#ifndef PACHI_TACTICS_NLIB_H
#define PACHI_TACTICS_NLIB_H

/* N-liberty semeai defense tactical checks. */

#include "board.h"
#include "debug.h"

void group_nlib_defense_check(board_t *b, group_t group, enum stone to_play, move_queue_t *q, int tag);

#endif
