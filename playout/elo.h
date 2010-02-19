#ifndef ZZGO_PLAYOUT_ELO_H
#define ZZGO_PLAYOUT_ELO_H

#include "stone.h"

struct board;
struct playout_policy;
struct probdist;

struct playout_policy *playout_elo_init(char *arg, struct board *b);

typedef void (*playout_elo_callbackp)(void *data, struct board *b, enum stone to_play, struct probdist *pd);
void playout_elo_callback(struct playout_policy *p, playout_elo_callbackp callback, void *data);

#endif
