#ifndef ZZGO_UCT_TREE_H
#define ZZGO_UCT_TREE_H

/* Management of UCT trees. See diagram below for the node structure.
 *
 * Two allocation methods are supported for the tree nodes:
 *
 * - calloc/free: each node is allocated with one calloc.
 *   After a move, all nodes except the subtree rooted at
 *   the played move are freed one by one with free().
 *   Since this can be very slow (seen 9s and loss on time because
 *   of this) the nodes are freed in a background thread.
 *   We still reserve enough memory for the next move in case
 *   the background thread doesn't free nodes fast enough.
 *
 * - fast_alloc: a large buffer is allocated once, and each
 *   node allocation takes some of this buffer. After a move
 *   is played, no memory if freed if the buffer still has
 *   enough free space. Otherwise the subtree rooted at the
 *   played move is copied to a temporary buffer, pruning it
 *   if necessary to fit in this small buffer. We copy by
 *   preference nodes with largest number of playouts.
 *   Then the temporary buffer is copied back to the original
 *   buffer, which has now plenty of space.
 *   Once the fast_alloc mode is proven reliable, the
 *   calloc/free method will be removed. */

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

	/* In case multiple threads walk the tree, is_expanded is set
	 * atomically. Only the first thread setting it expands the node.
	 * The node goes through 3 states:
	 *   1) children == null, is_expanded == false: leaf node
	 *   2) children == null, is_expanded == true: one thread currently expanding
	 *   2) children != null, is_expanded == true: fully expanded node */
	bool is_expanded;
};

struct tree {
	struct board *board;
	struct tree_node *root;
	struct board_symmetry root_symmetry;
	enum stone root_color;

	bool use_extra_komi;
	float extra_komi;

	/* We merge local (non-tenuki) sequences for both colors, occuring
	 * anywhere in the tree; nodes are created on-demand, special 'pass'
	 * nodes represent tenuki. Only u move_stats are used, prior and amaf
	 * is ignored. Values in root node are ignored. */
	/* The values in the tree can be either "raw" or "tempered"
	 * (representing difference against parent node in the main tree),
	 * controlled by local_tree setting. */
	struct tree_node *ltree_black;
	// Of course even in white tree, winrates are from b's perspective
	// as anywhere else. ltree_white has white-first sequences as children.
	struct tree_node *ltree_white;

	// Statistics
	int max_depth;
	volatile unsigned long nodes_size; // byte size of all allocated nodes
	unsigned long max_tree_size; // maximum byte size for entire tree, > 0 only for fast_alloc
	void *nodes; // nodes buffer, only for fast_alloc
};

/* Warning: all functions below except tree_expand_node & tree_leaf_node are THREAD-UNSAFE! */
struct tree *tree_init(struct board *board, enum stone color, unsigned long max_tree_size);
void tree_done(struct tree *tree);
void tree_dump(struct tree *tree, int thres);
void tree_save(struct tree *tree, struct board *b, int thres);
void tree_load(struct tree *tree, struct board *b);
struct tree *tree_copy(struct tree *tree);
void tree_merge(struct tree *dest, struct tree *src);
void tree_normalize(struct tree *tree, int factor);

void tree_expand_node(struct tree *tree, struct tree_node *node, struct board *b, enum stone color, struct uct *u, int parity);
struct tree_node *tree_lnode_for_node(struct tree *tree, struct tree_node *ni, struct tree_node *lni, int tenuki_d);

/* Warning: All these functions are THREAD-UNSAFE! */
struct tree_node *tree_get_node(struct tree *tree, struct tree_node *node, coord_t c, bool create);
void tree_promote_node(struct tree *tree, struct tree_node **node);
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

/* Leave always at least 10% memory free for the next move: */
#define MIN_FREE_MEM_PERCENT 10

#endif
