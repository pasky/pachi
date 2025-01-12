#ifndef PACHI_UCT_POLICY_H
#define PACHI_UCT_POLICY_H


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

typedef struct uct_policy {
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
} uct_policy_t;


#endif
