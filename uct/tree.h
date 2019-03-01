#ifndef PACHI_UCT_TREE_H
#define PACHI_UCT_TREE_H

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

/* TODO: Performance would benefit from a reorganization:
 * (i) Allocate all children of a node within a single block.
 * (ii) Keep all u stats together, and all amaf stats together.
 * Currently, rave_update is top source of cache misses, and
 * there is large memory overhead for having all nodes separate. */

typedef struct tree_node {
	hash_t hash;
	struct tree_node *parent, *sibling, *children;

	/*** From here on, struct is saved/loaded from opening tbook */

	move_stats_t u;
	move_stats_t prior;
	/* XXX: Should be way for policies to add their own stats */
	move_stats_t amaf;
	/* Stats before starting playout; used for distributed engine. */
	move_stats_t pu;
	/* Criticality information; information about final board owner
	 * of the tree coordinate corresponding to the node */
	move_stats_t winner_owner; // owner == winner
	move_stats_t black_owner; // owner == black

	/* coord is usually coord_t, but this is very space-sensitive. */
#define node_coord(n) ((int) (n)->coord)
	short coord;

	unsigned short depth; // just for statistics

	/* Number of parallel descents going through this node at the moment.
	* Used for virtual loss computation. */
	signed char descents;

	/* Common Fate Graph distance from parent, but at most TREE_NODE_D_MAX+1 */
#define TREE_NODE_D_MAX 3
	unsigned char d;

#define TREE_HINT_INVALID 1 // don't go to this node, invalid move
#define TREE_HINT_DCNN    2 // node has dcnn priors
	unsigned char hints;

	/* In case multiple threads walk the tree, is_expanded is set
	* atomically. Only the first thread setting it expands the node.
	* The node goes through 3 states:
	*   1) children == null, is_expanded == false: leaf node
	*   2) children == null, is_expanded == true: one thread currently expanding
	*   2) children != null, is_expanded == true: fully expanded node */
	bool is_expanded;
} tree_node_t;

struct tree_hash;

typedef struct {
	board_t *board;
	tree_node_t *root;
	board_symmetry_t root_symmetry;
	enum stone root_color;

	/* Whether to use any extra komi during score counting. This is
	 * tree-specific variable since this can arbitrarily change between
	 * moves. */
	bool use_extra_komi;
	/* A single-move-valid flag that marks a tree that is potentially
	 * badly skewed and should be used with care. Currently, we never
	 * resign on untrustworthy_tree and do not reuse the tree on next
	 * move. */
	bool untrustworthy_tree;
	/* The value of applied extra komi. For DYNKOMI_LINEAR, this value
	 * is only informative, the actual value is computed per simulation
	 * based on leaf node depth. */
	floating_t extra_komi;
	/* Score in simulations, averaged over all branches, in the last
	 * search episode. */
	move_stats_t avg_score;

	/* We merge local (non-tenuki) sequences for both colors, occuring
	 * anywhere in the tree; nodes are created on-demand, special 'pass'
	 * nodes represent tenuki. Only u move_stats are used, prior and amaf
	 * is ignored. Values in root node are ignored. */
	/* The value corresponds to black-to-play as usual; i.e. if white
	 * succeeds in its replies, the values will be low. */
	tree_node_t *ltree_black;
	/* ltree_white has white-first sequences as children. */
	tree_node_t *ltree_white;
	/* Aging factor; 2 means halve all playout values after each turn.
	 * 1 means don't age at all. */
	floating_t ltree_aging;

	/* Hash table used when working as slave for the distributed engine.
	 * Maps coordinate path to tree node. */
	struct tree_hash *htable;
	int hbits;

	// Statistics
	int max_depth;
	volatile size_t nodes_size; // byte size of all allocated nodes
	size_t max_tree_size; // maximum byte size for entire tree, > 0 only for fast_alloc
	size_t max_pruned_size;
	size_t pruning_threshold;
	void *nodes; // nodes buffer, only for fast_alloc
} tree_t;

/* Warning: all functions below except tree_expand_node & tree_leaf_node are THREAD-UNSAFE! */
tree_t *tree_init(board_t *board, enum stone color, size_t max_tree_size,
		       size_t max_pruned_size, size_t pruning_threshold, floating_t ltree_aging, int hbits);
void tree_done(tree_t *tree);
void tree_dump(tree_t *tree, double thres);
void tree_save(tree_t *tree, board_t *b, int thres);
void tree_load(tree_t *tree, board_t *b);

tree_node_t *tree_get_node(tree_node_t *parent, coord_t c);
tree_node_t *tree_get_node2(tree_t *tree, tree_node_t *parent, coord_t c, bool create);
tree_node_t *tree_garbage_collect(tree_t *tree, tree_node_t *node);
void tree_promote_node(tree_t *tree, tree_node_t **node);
bool tree_promote_at(tree_t *tree, board_t *b, coord_t c, int *reason);

void tree_expand_node(tree_t *tree, tree_node_t *node, board_t *b, enum stone color, struct uct *u, int parity);
tree_node_t *tree_lnode_for_node(tree_t *tree, tree_node_t *ni, tree_node_t *lni, int tenuki_d);

static bool tree_leaf_node(tree_node_t *node);

#define tree_node_parity(tree, node) \
	((((node)->depth ^ (tree)->root->depth) & 1) ? -1 : 1)

/* Get black parity from parity within the tree. */
#define tree_parity(tree, parity) \
	(tree->root_color == S_WHITE ? (parity) : -1 * (parity))

/* Get a 0..1 value to maximize; @parity is parity within the tree. */
#define tree_node_get_value(tree, parity, value) \
	(tree_parity(tree, parity) > 0 ? value : 1 - value)

static inline bool
tree_leaf_node(tree_node_t *node)
{
	return !(node->children);
}

static inline floating_t
tree_node_criticality(const tree_t *t, const tree_node_t *node)
{
	/* cov(player_gets, player_wins) =
	 * [The argument: If 'gets' and 'wins' is uncorrelated, b_gets * b_wins
	 * is valid way to obtain winner_gets. The more correlated it is, the
	 * more distorted the result.]
	 * = winner_gets - (b_gets * b_wins + w_gets * w_wins)
	 * = winner_gets - (b_gets * b_wins + (1 - b_gets) * (1 - b_wins))
	 * = winner_gets - (b_gets * b_wins + 1 - b_gets - b_wins + b_gets * b_wins)
	 * = winner_gets - (2 * b_gets * b_wins - b_gets - b_wins + 1) */
	return node->winner_owner.value
		- (2 * node->black_owner.value * node->u.value
		   - node->black_owner.value - node->u.value + 1);
}

#endif
