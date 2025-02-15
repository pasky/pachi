#ifndef PACHI_UCT_POLICY_H
#define PACHI_UCT_POLICY_H


typedef tree_node_t* (*uctp_choose)(uct_policy_t *p, tree_node_t *node, board_t *b, enum stone color, coord_t exclude);
typedef floating_t   (*uctp_evaluate)(uct_policy_t *p, tree_t *tree, tree_node_t *node, int parity);
typedef tree_node_t* (*uctp_descend)(uct_policy_t *p, tree_t *tree, tree_node_t *node, int parity, bool allow_pass);
typedef tree_node_t* (*uctp_winner)(uct_policy_t *p, tree_t *tree, tree_node_t *node);
typedef void         (*uctp_prior)(uct_policy_t *p, tree_t *tree, tree_node_t *node, board_t *b, enum stone color, int parity);
typedef void         (*uctp_update)(uct_policy_t *p, tree_t *tree, tree_node_t *node, enum stone node_color, enum stone player_color, playout_amafmap_t *amaf, board_t *final_board, floating_t result);
typedef void         (*uctp_done)(uct_policy_t *p);

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
