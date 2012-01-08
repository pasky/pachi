#ifndef PACHI_ENGINE_H
#define PACHI_ENGINE_H

#include "board.h"
#include "move.h"
#include "gtp.h"

struct move_queue;

typedef enum parse_code (*engine_notify)(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply);
typedef char *(*engine_notify_play)(struct engine *e, struct board *b, struct move *m);
typedef char *(*engine_undo)(struct engine *e, struct board *b);
typedef char *(*engine_result)(struct engine *e, struct board *b);
typedef char *(*engine_chat)(struct engine *e, struct board *b, char *cmd);
/* Generate a move. If pass_all_alive is true, <pass> shall be generated only
 * if all stones on the board can be considered alive, without regard to "dead"
 * considered stones. */
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive);
typedef char *(*engine_genmoves)(struct engine *e, struct board *b, struct time_info *ti, enum stone color,
				 char *args, bool pass_all_alive, void **stats_buf, int *stats_size);
/* Evaluate feasibility of player @color playing at @c. Will simulate
 * this move for time @ti, then return 1-max(opponent_win_likelihood). */
typedef floating_t (*engine_evaluate)(struct engine *e, struct board *b, struct time_info *ti, coord_t c, enum stone color);
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

	engine_notify notify;
	board_cprint printhook;
	engine_notify_play notify_play;
	engine_chat chat;
	engine_undo undo;
	engine_result result;
	engine_genmove genmove;
	engine_genmoves genmoves;
	engine_evaluate evaluate;
	engine_dead_group_list dead_group_list;
	engine_done done;
	void *data;
};

#endif
