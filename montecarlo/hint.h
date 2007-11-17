#ifndef ZZGO_MONTECARLO_HINT_H
#define ZZGO_MONTECARLO_HINT_H

#include "move.h"
#include "stone.h"

struct montecarlo;
struct board;

void domain_hint(struct montecarlo *mc, struct board *b, coord_t *urgent, enum stone our_real_color);

#endif
