#ifndef ZZGO_UCT_INTERNAL_H
#define ZZGO_UCT_INTERNAL_H

#include <signal.h> // sig_atomic_t

#include "debug.h"
#include "move.h"
#include "ownermap.h"
#include "playout.h"

struct tree;
struct tree_node;
struct uct_policy;
struct uct_prior;

/* Internal UCT structures */


/* Internal engine state. */
struct uct {
	int debug_level;
	int games, gamelen;
	float resign_ratio;
	float loss_threshold;
	bool pass_all_alive;
	int expand_p;
	bool playout_amaf, playout_amaf_nakade;
	bool amaf_prior;
	int playout_amaf_cutoff;
	int dumpthres;
	int force_seed;
	bool no_book;

	int threads;
	enum uct_thread_model {
		TM_NONE, /* <=> threads == 0 */
		TM_ROOT, /* Root parallelization. */
		TM_TREE, /* Tree parallelization w/o virtual loss. */
		TM_TREEVL, /* Tree parallelization with virtual loss. */
	} thread_model;
	bool parallel_tree;
	bool virtual_loss;

	int dynkomi;
	int dynkomi_mask;

	float val_scale;
	int val_points;
	bool val_extra;

	int random_policy_chance;
	int root_heuristic;

	char *banner;

	struct uct_policy *policy;
	struct uct_policy *random_policy;
	struct playout_policy *playout;
	struct uct_prior *prior;

	/* Used within frame of single genmove. */
	struct board_ownermap ownermap;

	/* Game state - maintained by setup_state(), reset_state(). */
	struct tree *t;
};

#define UDEBUGL(n) DEBUGL_(u->debug_level, n)

extern volatile sig_atomic_t uct_halt;
extern __thread int thread_id;

bool uct_pass_is_safe(struct uct *u, struct board *b, enum stone color);


typedef struct tree_node *(*uctp_choose)(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);
typedef float (*uctp_evaluate)(struct uct_policy *p, void **state, struct tree *tree, struct tree_node *node, int parity);
typedef struct tree_node *(*uctp_descend)(struct uct_policy *p, void **state, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);
typedef void (*uctp_prior)(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int parity);
typedef void (*uctp_update)(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone node_color, enum stone player_color, struct playout_amafmap *amaf, float result);

struct uct_policy {
	struct uct *uct;
	uctp_choose choose;
	uctp_evaluate evaluate;
	uctp_descend descend;
	uctp_update update;
	uctp_prior prior;
	bool wants_amaf;
	void *data;
};

#endif
