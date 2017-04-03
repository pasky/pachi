#ifndef PACHI_ENGINE_H
#define PACHI_ENGINE_H

#include "board.h"
#include "move.h"
#include "gtp.h"

struct move_queue;

typedef enum parse_code (*engine_notify_t)(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply);
typedef void (*engine_board_print_t)(struct engine *e, struct board *b, FILE *f);
typedef char *(*engine_notify_play_t)(struct engine *e, struct board *b, struct move *m, char *enginearg);
typedef char *(*engine_undo_t)(struct engine *e, struct board *b);
typedef char *(*engine_result_t)(struct engine *e, struct board *b);
typedef char *(*engine_chat_t)(struct engine *e, struct board *b, bool in_game, char *from, char *cmd);
/* Generate a move. If pass_all_alive is true, <pass> shall be generated only
 * if all stones on the board can be considered alive, without regard to "dead"
 * considered stones. */
typedef coord_t *(*engine_genmove_t)(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive);
typedef void  (*engine_best_moves_t)(struct engine *e, struct board *b, struct time_info *ti, enum stone color, 
				     coord_t *best_c, float *best_r, int nbest);
typedef char *(*engine_genmoves_t)(struct engine *e, struct board *b, struct time_info *ti, enum stone color,
				 char *args, bool pass_all_alive, void **stats_buf, int *stats_size);
/* Evaluate feasibility of player @color playing at all free moves. Will
 * simulate each move from b->f[i] for time @ti, then set
 * 1-max(opponent_win_likelihood) in vals[i]. */
typedef void (*engine_evaluate_t)(struct engine *e, struct board *b, struct time_info *ti, floating_t *vals, enum stone color);
/* One dead group per queued move (coord_t is (ab)used as group_t). */
typedef void (*engine_dead_group_list_t)(struct engine *e, struct board *b, struct move_queue *mq);
/* Pause any background thinking being done, but do not tear down
 * any data structures yet. */
typedef void (*engine_stop_t)(struct engine *e);
/* e->data and e will be free()d by caller afterwards. */
typedef void (*engine_done_t)(struct engine *e);

/* GoGui hooks */
typedef float (*engine_owner_map_t)(struct engine *e, struct board *b, coord_t c);
typedef void (*engine_live_gfx_hook_t)(struct engine *e);

/* This is engine data structure. A new engine instance is spawned
 * for each new game during the program lifetime. */
struct engine {
	char *name;
	char *comment;

	/* If set, do not reset the engine state on clear_board. */
	bool keep_on_clear;

	engine_notify_t notify;
	engine_board_print_t board_print;
	engine_notify_play_t notify_play;
	engine_chat_t chat;
	engine_undo_t undo;
	engine_result_t result;
	engine_genmove_t genmove;
	engine_genmoves_t genmoves;
	engine_best_moves_t best_moves;
	engine_evaluate_t evaluate;
	engine_dead_group_list_t dead_group_list;
	engine_stop_t stop;
	engine_done_t done;
	engine_owner_map_t owner_map;
	engine_live_gfx_hook_t live_gfx_hook;
	void *data;
};

static inline void
engine_board_print(struct engine *e, struct board *b, FILE *f)
{
	(e->board_print ? e->board_print(e, b, f) : board_print(b, f));
}

static inline void
engine_done(struct engine *e)
{
	if (e->done) e->done(e);
	if (e->data) free(e->data);
	free(e);
}

/* For engines best_move(): Add move @c with prob @r to best moves @best_c, @best_r */
static inline void
best_moves_add(coord_t c, float r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++)
		if (r > best_r[i]) {
			for (int j = nbest - 1; j > i; j--) { // shift
				best_r[j] = best_r[j - 1];
				best_c[j] = best_c[j - 1];
			}
			best_r[i] = r;
			best_c[i] = c;
			break;
		}
}


#endif
