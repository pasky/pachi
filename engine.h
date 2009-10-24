#ifndef ZZGO_ENGINE_H
#define ZZGO_ENGINE_H

#include "board.h"
#include "move.h"

struct engine;

typedef void (*engine_notify_play)(struct engine *e, struct board *b, struct move *m);
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, enum stone color);
typedef void (*engine_done_board_state)(struct engine *e, struct board *b);

struct engine {
	char *name;
	char *comment;
	board_cprint printhook;
	engine_notify_play notify_play;
	engine_genmove genmove;
	engine_done_board_state done_board_state;
	void *data;
};

#endif
