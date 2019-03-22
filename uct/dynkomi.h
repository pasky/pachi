#ifndef PACHI_UCT_DYNKOMI_H
#define PACHI_UCT_DYNKOMI_H

/* Dynamic computation of artificial komi values to stabilize the MCTS. */

#include "move.h"
#include "uct/internal.h"
#include "uct/tree.h"

/* Motivation: Monte Carlo Tree Search tends to produce unstable and
 * unreasonable results when playing in situation of extreme advantage
 * or * disadvantage, due to poor move selection becauce of low
 * signal-to-noise * ratio; notably, this occurs when playing in high
 * handicap game, burdening the computer with further disadvantage
 * against the strong human opponent. */

/* Here, we try to solve the problem by adding arbitrarily computed
 * komi values to the score. The used algorithm is transparent to the
 * rest of UCT implementation. */

struct board;
typedef struct uct_dynkomi uct_dynkomi_t;

/* Compute effective komi value for given color: Positive value
 * means giving komi, negative value means taking komi. */
#define komi_by_color(komi, color) ((color) == S_BLACK ? (komi) : -(komi))

/* Determine base dynamic komi for this genmove run. The returned
 * value is stored in tree->extra_komi and by itself used just for
 * user information. */
typedef floating_t (*uctd_permove)(uct_dynkomi_t *d, board_t *b, tree_t *tree);
/* Determine actual dynamic komi for this simulation (run on board @b
 * from node @node). In some cases, this function will just return
 * tree->extra_komi, in other cases it might want to adjust the komi
 * according to the actual move depth. */
typedef floating_t (*uctd_persim)(uct_dynkomi_t *d, board_t *b, tree_t *tree, tree_node_t *node);
/* Destroy the uct_dynkomi structure. */
typedef void (*uctd_done)(uct_dynkomi_t *d);

struct uct_dynkomi {
	uct_t *uct;
	uctd_permove permove;
	uctd_persim persim;
	uctd_done done;
	void *data;

	/* Game state for dynkomi use: */
	/* Information on average score at the simulation end (black's
	 * perspective) since last dynkomi adjustment. */
	move_stats_t score;
	/* Information on average winrate of simulations since last
	 * dynkomi adjustment. */
	move_stats_t value;
};

uct_dynkomi_t *uct_dynkomi_init_none(uct_t *u, char *arg, board_t *b);
uct_dynkomi_t *uct_dynkomi_init_linear(uct_t *u, char *arg, board_t *b);
uct_dynkomi_t *uct_dynkomi_init_adaptive(uct_t *u, char *arg, board_t *b);

#endif
