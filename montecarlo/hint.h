#ifndef ZZGO_MONTECARLO_HINT_H
#define ZZGO_MONTECARLO_HINT_H

#include "move.h"
#include "stone.h"

struct montecarlo;
struct board;

coord_t domain_hint(struct montecarlo *mc, struct board *b, enum stone our_real_color);

#endif
