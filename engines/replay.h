#ifndef PACHI_REPLAY_REPLAY_H
#define PACHI_REPLAY_REPLAY_H

#include "engine.h"

coord_t replay_sample_moves(engine_t *e, board_t *b, enum stone color, 
			    int *played, int *pmost_played);
void engine_replay_init(engine_t *e, char *arg, board_t *b);

#endif
