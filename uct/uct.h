#ifndef PACHI_UCT_UCT_H
#define PACHI_UCT_UCT_H

#include "engine.h"

void engine_uct_init(engine_t *e, board_t *b);

bool   uct_gentbook(engine_t *e, board_t *b, time_info_t *ti, enum stone color);
void   uct_dumptbook(engine_t *e, board_t *b, enum stone color);
size_t uct_default_max_tree_size(void);

#endif
