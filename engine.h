#ifndef ZZGO_ENGINE_H
#define ZZGO_ENGINE_H

#include "move.h"

struct board;

typedef struct coord *(*engine_genmove)(struct board *b, enum stone color);

struct engine {
	char *name;
	char *comment;
	engine_genmove genmove;
};

#endif
