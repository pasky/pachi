#ifndef ZZGO_UCT_TREE_H
#define ZZGO_UCT_TREE_H

#include <stdbool.h>
#include <pthread.h>
#include "move.h"
#include "stats.h"
#include "probdist.h"

struct board;
struct uct;

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

	/*** From here on, struct is saved/loaded from opening book */

	int depth; // just for statistics

	coord_t coord;
	/* Common Fate Graph distance from parent, but at most TREE_NODE_D_MAX+1 */
	int d;
#define TREE_NODE_D_MAX 3

	struct move_stats u;
	struct move_stats prior;
	/* XXX: Should be way for policies to add their own stats */
	struct move_stats amaf;
	/* Stats before starting playout; used for multi-thread normalization. */
	struct move_stats pu, pamaf;

#define TREE_HINT_INVALID 1 // don't go to this node, invalid move
	int hints;
};

struct tree {
	struct board *board;
	struct tree_node *root;
	struct board_symmetry root_symmetry;
	enum stone root_color;
	float extra_komi;

	// In case multiple threads walk the tree, this mutex is used
	// to prevent them from expanding the same node in parallel.
	pthread_mutex_t expansion_mutex;

	// Summary statistics of good black, white moves in the tree
	struct move_stats *chvals; // [bsize2] root children
	struct move_stats *chchvals; // [bsize2] root children's children

	// Statistics
	int max_depth;
};

struct tree *tree_init(struct board *board, enum stone color);
void tree_done(struct tree *tree);
void tree_dump(struct tree *tree, int thres);
void tree_save(struct tree *tree, struct board *b, int thres);
void tree_load(struct tree *tree, struct board *b);
struct tree *tree_copy(struct tree *tree);
void tree_merge(struct tree *dest, struct tree *src);
void tree_normalize(struct tree *tree, int factor);

/* Warning: All these functions are THREAD-UNSAFE! */
void tree_expand_node(struct tree *tree, struct tree_node *node, struct board *b, enum stone color, struct uct *u, int parity);
void tree_delete_node(struct tree *tree, struct tree_node *node);
void tree_promote_node(struct tree *tree, struct tree_node *node);
bool tree_promote_at(struct tree *tree, struct board *b, coord_t c);

static bool tree_leaf_node(struct tree_node *node);

/* Get black parity from parity within the tree. */
#define tree_parity(tree, parity) \
	(tree->root_color == S_WHITE ? (parity) : -1 * (parity))

/* Get a 0..1 value to maximize; @parity is parity within the tree. */
#define tree_node_get_value(tree, parity, value) \
	(tree_parity(tree, parity) > 0 ? value : 1 - value)

static inline bool
tree_leaf_node(struct tree_node *node)
{
	return !(node->children);
}

#endif
