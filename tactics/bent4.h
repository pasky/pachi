#ifndef PACHI_TACTICS_BENT4_H
#define PACHI_TACTICS_BENT4_H

#include "playout.h"

/* Playout bent-four data */
typedef struct {
	int     moves;   /* Move number before filling bent-four */
	coord_t lib;     /* Last bent-four liberty */
	coord_t kill;    /* Move to kill after bent-four is captured */
} bent4_t;

/* Init playout bent-four data. */
void bent4_init(bent4_t *b4, board_t *b);

/* Init bent-four statics */
void bent4_statics_init(int boardsize);

/* Play one move handling bent-fours. */
coord_t bent4_play_move(bent4_t *b4, playout_t *playout, board_t *b, enum stone color);

#endif
