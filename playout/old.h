#ifndef ZZGO_PLAYOUT_OLD_H
#define ZZGO_PLAYOUT_OLD_H

#include "move.h"
#include "stone.h"

struct montecarlo;
struct board;

coord_t playout_old(struct montecarlo *mc, struct board *b, enum stone our_real_color);

#endif
