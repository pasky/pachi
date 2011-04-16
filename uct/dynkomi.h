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
struct tree;
struct tree_node;
struct uct;
struct uct_dynkomi;

/* Compute effective komi value for given color: Positive value
 * means giving komi, negative value means taking komi. */
#define komi_by_color(komi, color) ((color) == S_BLACK ? (komi) : -(komi))

/* Determine base dynamic komi for this genmove run. The returned
 * value is stored in tree->extra_komi and by itself used just for
 * user information. */
typedef floating_t (*uctd_permove)(struct uct_dynkomi *d, struct board *b, struct tree *tree);
/* Determine actual dynamic komi for this simulation (run on board @b
 * from node @node). In some cases, this function will just return
 * tree->extra_komi, in other cases it might want to adjust the komi
 * according to the actual move depth. */
typedef floating_t (*uctd_persim)(struct uct_dynkomi *d, struct board *b, struct tree *tree, struct tree_node *node);
/* Destroy the uct_dynkomi structure. */
typedef void (*uctd_done)(struct uct_dynkomi *d);

struct uct_dynkomi {
	struct uct *uct;
	uctd_permove permove;
	uctd_persim persim;
	uctd_done done;
	void *data;

	/* Game state for dynkomi use: */
	/* Information on average score at the simulation end (black's
	 * perspective) since last dynkomi adjustment. */
	struct move_stats score;
	/* Information on average winrate of simulations since last
	 * dynkomi adjustment. */
	struct move_stats value;
};

struct uct_dynkomi *uct_dynkomi_init_none(struct uct *u, char *arg, struct board *b);
struct uct_dynkomi *uct_dynkomi_init_linear(struct uct *u, char *arg, struct board *b);
struct uct_dynkomi *uct_dynkomi_init_adaptive(struct uct *u, char *arg, struct board *b);

#endif
