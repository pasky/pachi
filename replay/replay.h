#ifndef PACHI_REPLAY_REPLAY_H
#define PACHI_REPLAY_REPLAY_H

#include "engine.h"

coord_t replay_sample_moves(struct engine *e, struct board *b, enum stone color, 
			    int *played, int *pmost_played);
struct engine *engine_replay_init(char *arg, struct board *b);

#endif
