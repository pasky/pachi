#ifndef PACHI_ENGINE_H
#define PACHI_ENGINE_H

#include "board.h"
#include "move.h"
#include "gtp.h"

struct move_queue;

typedef enum parse_code (*engine_notify)(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply);
typedef char *(*engine_notify_play)(struct engine *e, struct board *b, struct move *m, char *enginearg);
typedef char *(*engine_undo)(struct engine *e, struct board *b);
typedef char *(*engine_result)(struct engine *e, struct board *b);
typedef char *(*engine_chat)(struct engine *e, struct board *b, bool in_game, char *from, char *cmd);
/* Generate a move. If pass_all_alive is true, <pass> shall be generated only
 * if all stones on the board can be considered alive, without regard to "dead"
 * considered stones. */
typedef coord_t *(*engine_genmove)(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive);
typedef char *(*engine_genmoves)(struct engine *e, struct board *b, struct time_info *ti, enum stone color,
				 char *args, bool pass_all_alive, void **stats_buf, int *stats_size);
/* Evaluate feasibility of player @color playing at all free moves. Will
 * simulate each move from b->f[i] for time @ti, then set
 * 1-max(opponent_win_likelihood) in vals[i]. */
typedef void (*engine_evaluate)(struct engine *e, struct board *b, struct time_info *ti, floating_t *vals, enum stone color);
/* One dead group per queued move (coord_t is (ab)used as group_t). */
typedef void (*engine_dead_group_list)(struct engine *e, struct board *b, struct move_queue *mq);
/* Pause any background thinking being done, but do not tear down
 * any data structures yet. */
typedef void (*engine_stop)(struct engine *e);
/* e->data and e will be free()d by caller afterwards. */
typedef void (*engine_done)(struct engine *e);

/* GoGui hooks */
typedef float (*engine_owner_map)(struct engine *e, struct board *b, coord_t c);
typedef void (*engine_best_moves)(struct engine *e, struct board *b, enum stone color);
typedef void (*engine_live_gfx_hook)(struct engine *e);

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
	engine_stop stop;
	engine_done done;
	engine_owner_map owner_map;
	engine_best_moves best_moves;
	engine_live_gfx_hook live_gfx_hook;
	void *data;
};

#endif
