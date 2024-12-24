#ifndef PACHI_UCT_INTERNAL_H
#define PACHI_UCT_INTERNAL_H

/* Internal UCT structures */

#include "debug.h"
#include "move.h"
#include "ownermap.h"
#include "playout.h"
#include "stats.h"
#include "mq.h"
#include "pattern/pattern.h"

typedef struct uct_prior uct_prior_t;
typedef struct uct_dynkomi uct_dynkomi_t;
typedef struct uct_pluginset uct_pluginset_t;
typedef struct uct_policy uct_policy_t;
typedef struct tree tree_t;
typedef struct tree_node tree_node_t;

typedef enum uct_reporting {
	UR_TEXT,
	UR_JSON,
	UR_JSON_BIG,
	UR_LEELA_ZERO,
} uct_reporting_t;

typedef enum uct_thread_model {
	TM_TREE, /* Tree parallelization w/o virtual loss. */
	TM_TREEVL, /* Tree parallelization with virtual loss. */
} uct_thread_model_t;

/* Internal engine state. */
typedef struct uct {
	int debug_level;
	enum uct_reporting reporting_opt;  /* original value */
	enum uct_reporting reporting;
	int    reportfreq_playouts;
	double reportfreq_time;
	FILE*  report_fh;

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

	/* Memory management */
	bool auto_alloc;
	size_t tree_size;
	size_t max_tree_size_opt;
	size_t max_mem;
	
	int mercymin;
	int significant_threshold;
	bool genmove_reset_tree;

	int threads;
	enum uct_thread_model thread_model;
	int virtual_loss;
	enum stone my_color;

	/* Current search flags */
	int search_flags;
	
	bool pondering_opt;                /* User wants pondering */
	int     dcnn_pondering_prior;      /* Prior next move guesses */
	int     dcnn_pondering_mcts;       /* Genmove next move guesses */
	coord_t dcnn_pondering_mcts_c[20];
	
	int fuseki_end;
	int yose_start;

	int dynkomi_mask;
	int dynkomi_interval;
	uct_dynkomi_t *dynkomi;
	floating_t initial_extra_komi;

	floating_t val_scale;
	int val_points;
	bool val_extra;
	bool val_byavg;
	bool val_bytemp;
	floating_t val_bytemp_min;

	int random_policy_chance;

	struct {
		int level;
		int playouts;
	} debug_after;

	uct_policy_t *policy;
	uct_policy_t *random_policy;
	playout_policy_t *playout;
	uct_prior_t *prior;
	uct_pluginset_t *plugins;
	pattern_config_t pc;

	/* Used within frame of single genmove. */
	ownermap_t ownermap;
#ifdef JOSEKIFIX
	ownermap_t prev_ownermap;
#endif
	int  raw_playouts_per_sec;  /* current raw playouts per second (mcowner) */
	bool allow_pass;    /* allow pass in uct descent */

	/* Distributed engine */
	bool slave; /* Act as slave in distributed engine. */
#ifdef DISTRIBUTED
	int max_slaves; /* Optional, -1 if not set */
	int slave_index; /* 0..max_slaves-1, or -1 if not set */

	/* Used for coordination among slaves of the distributed engine. */
	int stats_hbits;
	int shared_nodes;
	int shared_levels;
	double stats_delay; /* stored in seconds */
	int played_own;
	int played_all; /* games played by all slaves */
#endif

	/* Saved dead groups, for final_status_list dead */
	move_queue_t dead_groups;
	int pass_moveno;
	
	/* Timing, stats */
	double mcts_time;
	int expanded_nodes;

	/* Game state - maintained by setup_state(), reset_state(). */
	board_t *main_board;
	tree_t *t;
	bool tree_ready;
} uct_t;


/* Whether search tree carries over for next move under current uct settings.
 * Only case where tree can't be reused is the default case: dcnn without pondering. */
#define reusing_tree(u, b)	(! ((using_dcnn(b) && !u->pondering_opt) ||			\
				    (u->t && u->t->untrustworthy_tree)   ||			\
				    u->genmove_reset_tree))


#ifdef DISTRIBUTED
#define stats_hbits(u)			((u)->stats_hbits)
#define played_all(u)			((u)->played_all)
#else
#define stats_hbits(u)			(0)
#define played_all(u)			(0)
#endif

#define UDEBUGL(n) DEBUGL_(u->debug_level, n)

bool uct_pass_is_safe(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, move_queue_t *dead, char **msg, bool log);
void uct_genmove_setup(uct_t *u, board_t *b, enum stone color);
void uct_pondering_stop(uct_t *u);
void uct_get_best_moves(uct_t *u, coord_t *best_c, float *best_r, int nbest, bool winrates, int min_playouts);
void uct_mcowner_playouts(uct_t *u, board_t *b, enum stone color);
void uct_tree_size_init(uct_t *u, size_t tree_size);


/* This is the state used for descending the tree; we use this wrapper
 * structure in order to be able to easily descend in multiple trees
 * in parallel (e.g. main tree and local tree) or compute cummulative
 * "path value" throughout the tree descent. */
typedef struct {
	/* Active tree nodes: */
	tree_node_t *node; /* Main tree. */
	/* Value of main tree node (with all value factors, but unbiased
	 * - without exploration factor), from black's perspective. */
	move_stats_t value;
} uct_descent_t;

#define uct_descent(node)  { node }

typedef tree_node_t *(*uctp_choose)(uct_policy_t *p, tree_node_t *node, board_t *b, enum stone color, coord_t exclude);
typedef floating_t (*uctp_evaluate)(uct_policy_t *p, tree_t *tree, uct_descent_t *descent, int parity);
typedef void (*uctp_descend)(uct_policy_t *p, tree_t *tree, uct_descent_t *descent, int parity, bool allow_pass);
typedef void (*uctp_winner)(uct_policy_t *p, tree_t *tree, uct_descent_t *descent);
typedef void (*uctp_prior)(uct_policy_t *p, tree_t *tree, tree_node_t *node, board_t *b, enum stone color, int parity);
typedef void (*uctp_update)(uct_policy_t *p, tree_t *tree, tree_node_t *node, enum stone node_color, enum stone player_color, playout_amafmap_t *amaf, board_t *final_board, floating_t result);
typedef void (*uctp_done)(uct_policy_t *p);

struct uct_policy {
	uct_t *uct;
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
