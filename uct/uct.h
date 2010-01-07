#ifndef ZZGO_UCT_UCT_H
#define ZZGO_UCT_UCT_H

#include "engine.h"
#include "move.h"

struct engine *engine_uct_init(char *arg, struct board *b);

struct board;
bool uct_genbook(struct engine *e, struct board *b, enum stone color);
void uct_dumpbook(struct engine *e, struct board *b, enum stone color);

#endif
