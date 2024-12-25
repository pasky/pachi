#ifndef PACHI_JOSEKIFIXLOAD_H
#define PACHI_JOSEKIFIXLOAD_H

#ifdef JOSEKIFIX

#include "engine.h"
#include "josekifix/josekifix.h"

extern override_t  *joseki_overrides;
extern override2_t *joseki_overrides2;
extern override_t  *logged_variations;
extern override2_t *logged_variations2;

void josekifixload_engine_init(engine_t *e, board_t *b);

bool josekifix_load(void);


#endif

#endif
