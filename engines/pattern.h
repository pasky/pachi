#ifndef PACHI_PATTERN_ENGINE_H
#define PACHI_PATTERN_ENGINE_H

#include "engine.h"
#include "../pattern.h"

void pattern_engine_init(engine_t *e, board_t *b);
pattern_config_t *pattern_engine_get_pc(engine_t *e);
bool pattern_engine_matched_locally(engine_t *e);

#endif
