#ifndef PACHI_UCT_INTERNAL_H
#define PACHI_UCT_INTERNAL_H

/* Internal UCT structures */

#include "debug.h"
#include "move.h"
#include "ownermap.h"
#include "pattern.h"
#include "patternsp.h"
#include "patternprob.h"
#include "playout.h"
#include "stats.h"
#include "mq.h"

struct tree;
struct tree_node;
struct uct_policy;
struct uct_prior;
struct uct_dynkomi;
struct uct_pluginset;

/* Internal engine state. */
struct uct {
	int debug_level;
	enum uct_reporting {
		UR_TEXT,
		UR_JSON,
		UR_JSON_BIG,
		UR_LEELAZ,
	} reporting;
	int reportfreq;

	int games, gamelen;
	floating_t resign_threshold, sure_win_threshold;
	double best2_ratio, bestr_ratio;
	floating_t max_maintime_ratio;
	bool pass_all_alive; /* Current value */
	bool allow_losing_pass;
	bool territory_scoring;
	int expand_p;
	bool playout_amaf;
	bool amaf_prior;
	int playout_amaf_cutoff;
	double dumpthres;
	int force_seed;
	bool no_tbook;
	bool fast_alloc;
	size_t max_tree_size;
	size_t max_pruned_size;
	size_t pruning_threshold;
	int mercymin;
	int significant_threshold;
	bool genmove_reset_tree;

	int threads;
	enum uct_thread_model {
		TM_TREE, /* Tree parallelization w/o virtual loss. */
		TM_TREEVL, /* Tree parallelization with virtual loss. */
	} thread_model;
	int virtual_loss;
	bool slave; /* Act as slave in distributed engine. */
	int max_slaves; /* Optional, -1 if not set */
	int slave_index; /* 0..max_slaves-1, or -1 if not set */
	enum stone my_color;

	bool pondering_opt;                /* User wants pondering */
	bool pondering;                    /* Actually pondering now */
	bool genmove_pondering;            /* Regular pondering (after a genmove) */
	int     dcnn_pondering_prior;      /* Prior next move guesses */
	int     dcnn_pondering_mcts;       /* Genmove next move guesses */
	coord_t dcnn_pondering_mcts_c[20];
	
	int fuseki_end;
	int yose_start;

	int dynkomi_mask;
	int dynkomi_interval;
	struct uct_dynkomi *dynkomi;
	floating_t initial_extra_komi;

	floating_t val_scale;
	int val_points;
	bool val_extra;
	bool val_byavg;
	bool val_bytemp;
	floating_t val_bytemp_min;

	int random_policy_chance;
	bool local_tree;
	int tenuki_d;
	floating_t local_tree_aging;
#define LTREE_PLAYOUTS_MULTIPLIER 100
	floating_t local_tree_depth_decay;
	bool local_tree_allseq;
	bool local_tree_neival;
	enum {
		LTE_ROOT,
		LTE_EACH,
		LTE_TOTAL,
	} local_tree_eval;
	bool local_tree_rootchoose;

	struct {
		int level;
		int playouts;
	} debug_after;

	char *banner;

	struct uct_policy *policy;
	struct uct_policy *random_policy;
	struct playout_policy *playout;
	struct uct_prior *prior;
	struct uct_pluginset *plugins;
	struct pattern_config pc;

	/* Used within frame of single genmove. */
	struct ownermap ownermap;

	/* Used for coordination among slaves of the distributed engine. */
	int stats_hbits;
	int shared_nodes;
	int shared_levels;
	double stats_delay; /* stored in seconds */
	int played_own;
	int played_all; /* games played by all slaves */

	/* Saved dead groups, for final_status_list dead */
	struct move_queue dead_groups;
	int pass_moveno;
	
	/* Timing */
	double mcts_time_start;

	/* Game state - maintained by setup_state(), reset_state(). */
	struct tree *t;
	bool tree_ready;
};

#define UDEBUGL(n) DEBUGL_(u->debug_level, n)

bool uct_pass_is_safe(struct uct *u, struct board *b, enum stone color, bool pass_all_alive, char **msg);
void uct_prepare_move(struct uct *u, struct board *b, enum stone color);
void uct_genmove_setup(struct uct *u, struct board *b, enum stone color);
void uct_pondering_stop(struct uct *u);
void uct_get_best_moves(struct uct *u, coord_t *best_c, float *best_r, int nbest, bool winrates);
void uct_get_best_moves_at(struct uct *u, struct tree_node *n, coord_t *best_c, float *best_r, int nbest, bool winrates);
void uct_mcowner_playouts(struct uct *u, struct board *b, enum stone color);

/* This is the state used for descending the tree; we use this wrapper
 * structure in order to be able to easily descend in multiple trees
 * in parallel (e.g. main tree and local tree) or compute cummulative
 * "path value" throughout the tree descent. */
struct uct_descent {
	/* Active tree nodes: */
	struct tree_node *node; /* Main tree. */
	struct tree_node *lnode; /* Local tree. */
	/* Value of main tree node (with all value factors, but unbiased
	 * - without exploration factor), from black's perspective. */
	struct move_stats value;
};


typedef struct tree_node *(*uctp_choose)(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color, coord_t exclude);
typedef floating_t (*uctp_evaluate)(struct uct_policy *p, struct tree *tree, struct uct_descent *descent, int parity);
typedef void (*uctp_descend)(struct uct_policy *p, struct tree *tree, struct uct_descent *descent, int parity, bool allow_pass);
typedef void (*uctp_winner)(struct uct_policy *p, struct tree *tree, struct uct_descent *descent);
typedef void (*uctp_prior)(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int parity);
typedef void (*uctp_update)(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone node_color, enum stone player_color, struct playout_amafmap *amaf, struct board *final_board, floating_t result);
typedef void (*uctp_done)(struct uct_policy *p);

struct uct_policy {
	struct uct *uct;
	uctp_choose choose;
	uctp_winner winner;
	uctp_evaluate evaluate;
	uctp_descend descend;
	uctp_update update;
	uctp_prior prior;
	uctp_done done;
	bool wants_amaf;
	void *data;
};


#endif
