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

/* uct_search_start() flags */
#define UCT_SEARCH_PONDERING		(1 << 0)  /* Pondering now */
#define UCT_SEARCH_GENMOVE_PONDERING	(1 << 1)  /* Regular pondering after a genmove */
#define UCT_SEARCH_WANT_GC		(1 << 2)  /* Garbage collect tree before pondering */
#define UCT_SEARCH_RESTARTED		(1 << 3)  /* Resuming search */

/* Search flags macros */
#define pondering(u)		((u)->search_flags & UCT_SEARCH_PONDERING)
#define genmove_pondering(u)	((u)->search_flags & UCT_SEARCH_GENMOVE_PONDERING)
#define search_want_gc(u)	((u)->search_flags & UCT_SEARCH_WANT_GC)
#define search_restarted(u)	((u)->search_flags & UCT_SEARCH_RESTARTED)

#define clear_search_want_gc(u)	 do { (u)->search_flags &= ~UCT_SEARCH_WANT_GC; } while(0)

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
	double mcts_time_start;
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

void uct_search_start(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti, uct_search_state_t *s, int flags);
uct_thread_ctx_t *uct_search_stop(void);

int uct_search_realloc_tree(uct_t *u, board_t *b, enum stone color, time_info_t *ti, uct_search_state_t *s);

void uct_search_progress(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti, uct_search_state_t *s, int playouts);

bool uct_search_check_stop(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti, uct_search_state_t *s, int i);

tree_node_t *uct_search_result(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, int played_games, int base_playouts, coord_t *best_coord);

#endif
