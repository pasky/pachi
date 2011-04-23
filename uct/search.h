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
struct uct_thread_ctx {
	int tid;
	struct uct *u;
	struct board *b;
	enum stone color;
	struct tree *t;
	unsigned long seed;
	int games;
	struct time_info *ti;
};


/* Progress information of the on-going MCTS search - when did we
 * last adjusted dynkomi, printed out stuff, etc. */
struct uct_search_state {
	/* Number of games simulated for this simulation before
	 * we started the search. (We have simulated them earlier.) */
	int base_playouts;
	/* Number of last dynkomi adjustment. */
	int last_dynkomi;
	/* Number of last game with progress print. */
	int last_print;
	/* Number of simulations to wait before next print. */
	int print_interval;
	/* Printed notification about full memory? */
	bool fullmem;

	struct time_stop stop;
	struct uct_thread_ctx *ctx;
};

int uct_search_games(struct uct_search_state *s);

void uct_search_start(struct uct *u, struct board *b, enum stone color, struct tree *t, struct time_info *ti, struct uct_search_state *s);
struct uct_thread_ctx *uct_search_stop(void);

void uct_search_progress(struct uct *u, struct board *b, enum stone color, struct tree *t, struct time_info *ti, struct uct_search_state *s, int i);

bool uct_search_check_stop(struct uct *u, struct board *b, enum stone color, struct tree *t, struct time_info *ti, struct uct_search_state *s, int i);

struct tree_node *uct_search_result(struct uct *u, struct board *b, enum stone color, bool pass_all_alive, int played_games, int base_playouts, coord_t *best_coord);

#endif
