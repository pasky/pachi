#ifndef ZZGO_UCT_SLAVE_H
#define ZZGO_UCT_SLAVE_H

#include "move.h"

struct board;
struct engine;
struct time_info;

char *uct_genmoves(struct engine *e, struct board *b, struct time_info *ti, enum stone color, char *args, bool pass_all_alive);

#endif
