#ifndef PACHI_PATTERNPLAY_PATTERNPLAY_H
#define PACHI_PATTERNPLAY_PATTERNPLAY_H

#include "engine.h"

struct engine *engine_patternplay_init(char *arg, struct board *b);
struct pattern_config *engine_patternplay_get_pc(struct engine *e);

#endif
