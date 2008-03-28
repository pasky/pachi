#ifndef ZZGO_UCT_TREE_H
#define ZZGO_UCT_TREE_H

#include <stdbool.h>
#include "move.h"

struct board;
struct uct_policy;

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

struct tree_node {
	hash_t hash;
	struct tree_node *parent, *sibling, *children;
	int depth; // just for statistics

	coord_t coord;

	int playouts; // # of playouts coming through this node
	int wins; // # of wins coming through this node
	float value; // wins/playouts
};

struct tree {
	struct tree_node *root;
	struct board *board;

	// Statistics
	int max_depth;
};

struct tree *tree_init(struct board *board, enum stone color);
void tree_done(struct tree *tree);
void tree_dump(struct tree *tree);

void tree_expand_node(struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int radar, struct uct_policy *policy);
void tree_delete_node(struct tree *tree, struct tree_node *node);
void tree_promote_node(struct tree *tree, struct tree_node *node);
bool tree_leaf_node(struct tree_node *node);

#endif
