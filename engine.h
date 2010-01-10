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
/* Generate a move. If pass_all_alive is true, <pass> shall be generated only
 * if all stones on the board can be considered alive, without regard to "dead"
 * considered stones. */
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, enum stone color, bool pass_all_alive);
/* One dead group per queued move (coord_t is (ab)used as group_t). */
typedef void (*engine_dead_group_list)(struct engine *e, struct board *b, struct move_queue *mq);
/* e->data and e will be free()d by caller afterwards. */
typedef void (*engine_done)(struct engine *e);

/* This is engine data structure. A new engine instance is spawned
 * for each new game during the program lifetime. */
struct engine {
	char *name;
	char *comment;

	/* If set, do not reset the engine state on clear_board. */
	bool keep_on_clear;

	board_cprint printhook;
	engine_notify_play notify_play;
	engine_chat chat;
	engine_genmove genmove;
	engine_dead_group_list dead_group_list;
	engine_done done;
	void *data;
};

#endif
