#ifndef ZZGO_ENGINE_H
#define ZZGO_ENGINE_H

#include "move.h"

struct board;
struct engine;

typedef void (*engine_notify_play)(struct engine *e, struct board *b, struct move *m);
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, enum stone color);

struct engine {
	char *name;
	char *comment;
	engine_notify_play notify_play;
	engine_genmove genmove;
	void *data;
};

#endif
