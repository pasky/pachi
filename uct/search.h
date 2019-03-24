#ifndef PACHI_UCT_SEARCH_H
#define PACHI_UCT_SEARCH_H

/* MCTS Search infrastructure. We juggle the search threads and
 * control search duration. */

/* uct.c provides the GTP interface and engine setup. */
/* walk.c controls repeated walking of the MCTS tree within
 * the search threads. */

#include <signal.h> // sig_atomic_t

#include "debug.h"
#include "move.h"
#include "ownermap.h"
#include "playout.h"
#include "timeinfo.h"
#include "uct/internal.h"

struct tree;
struct tree_node;

/* Internal UCT structures */

/* How often to inspect the tree from the main thread to check for playout
 * stop, progress reports, etc. (in seconds) */
#define TREE_BUSYWAIT_INTERVAL 0.1 /* 100ms */


/* Thread manager state */
extern volatile sig_atomic_t uct_halt;
extern bool thread_manager_running;

/* Search thread context */
typedef struct uct_thread_ctx {
	int tid;
	uct_t *u;
	board_t *b;
	enum stone color;
	tree_t *t;
	unsigned long seed;
	int games;
	time_info_t *ti;
	struct uct_search_state *s;
} uct_thread_ctx_t;


/* Progress information of the on-going MCTS search - when did we
 * last adjusted dynkomi, printed out stuff, etc. */
typedef struct uct_search_state {	
	int base_playouts;	  /* Number of games simulated for this simulation before
				   * we started the search. (We have simulated them earlier.) */
	int last_dynkomi;	  /* Number of playouts for last dynkomi adjustment. */
	int last_print_playouts;  /* Last progress print (playouts) */
	double last_print_time;   /* Last progress print (time) */
	bool fullmem;		  /* Printed notification about full memory? */

	time_stop_t stop;
	uct_thread_ctx_t *ctx;
} uct_search_state_t;


int uct_search_games(uct_search_state_t *s);

void uct_search_start(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti, uct_search_state_t *s);
uct_thread_ctx_t *uct_search_stop(void);

void uct_search_progress(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti, uct_search_state_t *s, int playouts);

bool uct_search_check_stop(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti, uct_search_state_t *s, int i);

tree_node_t *uct_search_result(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, int played_games, int base_playouts, coord_t *best_coord);

#endif
