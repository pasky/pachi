#ifndef PACHI_PATTERNPLAY_PATTERNPLAY_H
#define PACHI_PATTERNPLAY_PATTERNPLAY_H

#include "engine.h"
#include "pattern.h"

void engine_patternplay_init(engine_t *e, char *arg, board_t *b);
pattern_config_t *patternplay_get_pc(engine_t *e);
bool patternplay_matched_locally(engine_t *e);

#endif
