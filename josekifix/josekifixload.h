#ifndef PACHI_JOSEKIFIXLOAD_H
#define PACHI_JOSEKIFIXLOAD_H

#ifdef JOSEKIFIX

#include "engine.h"
#include "josekifix/joseki_override.h"

extern joseki_override_t  *joseki_overrides;
extern joseki_override2_t *joseki_overrides2;
extern joseki_override_t  *logged_variations;
extern joseki_override2_t *logged_variations2;

void josekifixload_engine_init(engine_t *e, board_t *b);

bool josekifix_load(void);


#endif

#endif
