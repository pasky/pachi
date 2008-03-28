#ifndef ZZGO_UCT_TREE_H
#define ZZGO_UCT_TREE_H

#include <stdbool.h>
#include "move.h"

struct board;

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
 *
 * And to account for different orders of play:
 *
 * parent <--- tree_node ===> boardpos --> children
 * sibling <--                         ----> rating
 */

struct tree_node {
	hash_t hash;
	struct tree_node *parent, *sibling;

	coord_t coord;
	struct boardpos *pos;
};

struct boardpos {
	hash_t hash;
	struct tree_node *children;

	int playouts; // # of playouts coming through this node
	int wins; // # of wins coming through this node
	float value; // wins/playouts
	int refcount; // how many nodes refer to this position
};

struct tree {
	struct tree_node *root;
	struct board *board;

	bool sharepos;

#define uct_hash_bits 25
#define uct_hash_mask ((1 << uct_hash_bits) - 1)
#define uct_hash_prev(i) ((i - 1) & uct_hash_mask)
#define uct_hash_next(i) ((i + 1) & uct_hash_mask)
	/* The positions are hashed by Zobrist hashes; when passing,
	 * the previous position is xor'd by color passing. */
	struct boardpos *positions[1 << uct_hash_bits];

	/* Statistics: Number of nodes reusing existing boardpos. */
	int reused_pos, total_pos;
};

struct tree *tree_init(struct board *board, enum stone color, bool sharepos);
void tree_done(struct tree *tree);
void tree_dump(struct tree *tree);

void tree_expand_node(struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int radar);
void tree_delete_node(struct tree *tree, struct tree_node *node);
void tree_promote_node(struct tree *tree, struct tree_node *node);
bool tree_leaf_node(struct tree_node *node);

#endif
