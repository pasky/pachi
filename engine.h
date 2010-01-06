#ifndef ZZGO_ENGINE_H
#define ZZGO_ENGINE_H

#include "board.h"
#include "move.h"

/* Used to signal the main loop that the engine structures need to be reset
 * (for fresh board). */
extern bool engine_reset;

struct engine;
struct move_queue;

typedef char *(*engine_notify_play)(struct engine *e, struct board *b, struct move *m);
typedef char *(*engine_chat)(struct engine *e, struct board *b, char *cmd);
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, enum stone color);
/* One dead group per queued move (coord_t is (ab)used as group_t). */
typedef void (*engine_dead_group_list)(struct engine *e, struct board *b, struct move_queue *mq);
typedef void (*engine_done_board_state)(struct engine *e, struct board *b);
/* Pachi exit hook. */
typedef void (*engine_finish)(struct engine *e);

/* This is engine data structure. A new engine instance is spawned
 * for each new game during the program lifetime. */
struct engine {
	char *name;
	char *comment;
	board_cprint printhook;
	engine_notify_play notify_play;
	engine_chat chat;
	engine_genmove genmove;
	engine_dead_group_list dead_group_list;
	engine_done_board_state done_board_state;
	engine_finish finish;
	void *data;
};

#endif
