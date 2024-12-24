#ifndef PACHI_JOSEKIFIX_ENGINE_H
#define PACHI_JOSEKIFIX_ENGINE_H

#ifdef JOSEKIFIX

#include "engine.h"

extern char *external_joseki_engine_cmd;
extern bool  external_joseki_engine_genmoved;


void josekifix_engine_init(engine_t *e, board_t *b);

coord_t   external_joseki_engine_genmove(board_t *b);
engine_t* josekifix_engine_if_needed(engine_t *uct, board_t *b);

/* For josekifixscan engine */
void set_fake_external_joseki_engine(void);

#endif

#endif
