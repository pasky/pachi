#ifndef PACHI_UCT_UCT_H
#define PACHI_UCT_UCT_H

#include "engine.h"
#include "move.h"

struct engine *engine_uct_init(char *arg, struct board *b);

struct board;
struct time_info;
bool uct_gentbook(struct engine *e, struct board *b, struct time_info *ti, enum stone color);
void uct_dumptbook(struct engine *e, struct board *b, enum stone color);

#endif
