#ifndef ZZGO_ENGINE_H
#define ZZGO_ENGINE_H

#include "board.h"
#include "move.h"

struct engine;
struct move_queue;

typedef void (*engine_notify_play)(struct engine *e, struct board *b, struct move *m);
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, enum stone color);
/* One dead group per queued move (coord_t is (ab)used as group_t). */
typedef void (*engine_dead_group_list)(struct engine *e, struct board *b, struct move_queue *mq);
typedef void (*engine_done_board_state)(struct engine *e, struct board *b);

struct engine {
	char *name;
	char *comment;
	board_cprint printhook;
	engine_notify_play notify_play;
	engine_genmove genmove;
	engine_dead_group_list dead_group_list;
	engine_done_board_state done_board_state;
	void *data;
};

#endif
