#ifndef PACHI_MONTECARLO_INTERNAL_H
#define PACHI_MONTECARLO_INTERNAL_H

#include "debug.h"
#include "move.h"

struct playout_policy;

/* Internal MonteCarlo structures */


/* Internal engine state. */
struct montecarlo {
	int debug_level;
	int gamelen;
	floating_t resign_ratio;
	int loss_threshold;
	struct playout_policy *playout;
};

#define MCDEBUGL(n) DEBUGL_(mc->debug_level, n)


/* Per-move playout statistics. */
struct move_stat {
	int games;
	int wins;
};

void board_stats_print(struct board *board, struct move_stat *moves, FILE *f);

#endif
