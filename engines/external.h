#ifndef PACHI_EXTERNAL_ENGINE_H
#define PACHI_EXTERNAL_ENGINE_H

#include "engine.h"

void external_engine_init(engine_t *e, board_t *b);
bool external_engine_started(engine_t *e);

void external_engine_undo(engine_t *e);
void external_engine_play(engine_t *e, coord_t c, enum stone color);


#endif
