#ifndef PACHI_EXTERNAL_ENGINE_H
#define PACHI_EXTERNAL_ENGINE_H

#include "engine.h"

void external_engine_init(engine_t *e, board_t *b);
bool external_engine_started(engine_t *e);
int  external_engine_send_cmd(engine_t *e, char *cmd, char **reply, char **error);


#endif
