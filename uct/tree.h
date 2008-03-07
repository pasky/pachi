#ifndef ZZGO_UCT_TREE_H
#define ZZGO_UCT_TREE_H

#include <stdbool.h>
#include "move.h"

struct board;

struct tree_node {
	/*
	 *            +------+
	 *            | node |
	 *            +------+
	 *          / <- parent
	 * +------+   v- sibling +------+
	 * | node | ------------ | node |
	 * +------+              +------+
	 *    | <- children          |
	 * +------+   +------+   +------+   +------+
	 * | node | - | node |   | node | - | node |
	 * +------+   +------+   +------+   +------+
	 */
	struct tree_node *parent, *sibling, *children;

	coord_t coord;
	int playouts; // # of playouts coming through this node
	int wins; // # of wins coming through this node
	float value; // wins/playouts
};

struct tree {
	struct tree_node *root;
	struct board *board;

	/* This is what the Modification of UCT with Patterns in Monte Carlo Go
	 * paper calls 'p'. Original UCB has this on 2, but this seems to
	 * produce way too wide searches; reduce this to get deeper and
	 * narrower readouts - try 0.2. */
	float explore_p;
};

struct tree *tree_init(struct board *board);
void tree_done(struct tree *tree);
void tree_dump(struct tree *tree);

void tree_expand_node(struct tree *tree, struct tree_node *node, struct board *b);
void tree_delete_node(struct tree_node *node);
void tree_promote_node(struct tree *tree, struct tree_node *node);
bool tree_leaf_node(struct tree_node *node);
struct tree_node *tree_best_child(struct tree_node *node);

struct tree_node *tree_uct_descend(struct tree *tree, struct tree_node *node, int parity, bool allow_pass);
void tree_uct_update(struct tree_node *node, int result);

#endif
