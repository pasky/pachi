#ifndef ZZGO_PLAYOUT_MOGGY_H
#define ZZGO_PLAYOUT_MOGGY_H

#include "move.h"
#include "stone.h"

struct montecarlo;
struct board;

coord_t playout_moggy(struct montecarlo *mc, struct board *b, enum stone our_real_color);

#endif
