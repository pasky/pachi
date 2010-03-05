#ifndef ZZGO_DISTRIBUTED_DISTRIBUTED_H
#define ZZGO_DISTRIBUTED_DISTRIBUTED_H

#include "engine.h"

#define DIST_GAMELEN 1000

#define force_reply(id)    ((id) + DIST_GAMELEN)
#define prevent_reply(id)  ((id) % DIST_GAMELEN)
#define move_number(id)    ((id) % DIST_GAMELEN)
#define reply_disabled(id) ((id) < DIST_GAMELEN)

struct engine *engine_distributed_init(char *arg, struct board *b);

#endif
