#ifndef PACHI_PATTERNPLAY_PATTERNPLAY_H
#define PACHI_PATTERNPLAY_PATTERNPLAY_H

#include "engine.h"

void engine_patternplay_init(struct engine *e, char *arg, struct board *b);
struct pattern_config *patternplay_get_pc(struct engine *e);
bool patternplay_matched_locally(struct engine *e);

#endif
