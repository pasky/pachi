#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"
#include "tactics/util.h"
#include "timeinfo.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"
#include "uct/slave.h"


/* Allocate tree node(s). The returned nodes are initialized with zeroes.
 * Returns NULL if not enough memory.
 * This function may be called by multiple threads in parallel. */
static struct tree_node *
tree_alloc_node(struct tree *t, int count, bool fast_alloc)
{
	struct tree_node *n = NULL;
	size_t nsize = count * sizeof(*n);
	unsigned long old_size = __sync_fetch_and_add(&t->nodes_size, nsize);

	if (fast_alloc) {
		if (old_size + nsize > t->max_tree_size)
			return NULL;
		assert(t->nodes != NULL);
		n = (struct tree_node *)(t->nodes + old_size);
		memset(n, 0, nsize);
	} else {
		n = calloc2(count, sizeof(*n));
	}
	return n;
}

/* Initialize a node at a given place in memory.
 * This function may be called by multiple threads in parallel. */
static void
tree_setup_node(struct tree *t, struct tree_node *n, coord_t coord, int depth)
{
	static volatile unsigned int hash = 0;
	n->coord = coord;
	n->depth = depth;
	/* n->hash is used only for debugging. It is very likely (but not
	 * guaranteed) to be unique. */
	hash_t h = n - (struct tree_node *)0;
	n->hash = (h << 32) + (hash++ & 0xffffffff);
	if (depth > t->max_depth)
		t->max_depth = depth;
}

/* Allocate and initialize a node. Returns NULL (fast_alloc mode)
 * or exits the main program if not enough memory.
 * This function may be called by multiple threads in parallel. */
static struct tree_node *
tree_init_node(struct tree *t, coord_t coord, int depth, bool fast_alloc)
{
	struct tree_node *n;
	n = tree_alloc_node(t, 1, fast_alloc);
	if (!n) return NULL;
	tree_setup_node(t, n, coord, depth);
	return n;
}

/* Create a tree structure. Pre-allocate all nodes if max_tree_size is > 0. */
struct tree *
tree_init(struct board *board, enum stone color, unsigned long max_tree_size,
	  unsigned long max_pruned_size, unsigned long pruning_threshold, floating_t ltree_aging, int hbits)
{
	struct tree *t = calloc2(1, sizeof(*t));
	t->board = board;
	t->max_tree_size = max_tree_size;
	t->max_pruned_size = max_pruned_size;
	t->pruning_threshold = pruning_threshold;
	if (max_tree_size != 0) {
		t->nodes = malloc2(max_tree_size);
		/* The nodes buffer doesn't need initialization. This is currently
		 * done by tree_init_node to spread the load. Doing a memset for the
		 * entire buffer here would be too slow for large trees (>10 GB). */
	}
	/* The root PASS move is only virtual, we never play it. */
	t->root = tree_init_node(t, pass, 0, t->nodes);
	t->root_symmetry = board->symmetry;
	t->root_color = stone_other(color); // to research black moves, root will be white

	t->ltree_black = tree_init_node(t, pass, 0, false);
	t->ltree_white = tree_init_node(t, pass, 0, false);
	t->ltree_aging = ltree_aging;

	t->hbits = hbits;
	if (hbits) t->htable = uct_htable_alloc(hbits);
	return t;
}


/* This function may be called by multiple threads in parallel on the
 * same tree, but not on node n. n may be detached from the tree but
 * must have been created in this tree originally.
 * It returns the remaining size of the tree after n has been freed. */
static unsigned long
tree_done_node(struct tree *t, struct tree_node *n)
{
	struct tree_node *ni = n->children;
	while (ni) {
		struct tree_node *nj = ni->sibling;
		tree_done_node(t, ni);
		ni = nj;
	}
	free(n);
	unsigned long old_size = __sync_fetch_and_sub(&t->nodes_size, sizeof(*n));
	return old_size - sizeof(*n);
}

struct subtree_ctx {
	struct tree *t;
	struct tree_node *n;
};

/* Worker thread for tree_done_node_detached(). Only for fast_alloc=false. */
static void *
tree_done_node_worker(void *ctx_)
{
	struct subtree_ctx *ctx = ctx_;
	char *str = coord2str(node_coord(ctx->n), ctx->t->board);

	unsigned long tree_size = tree_done_node(ctx->t, ctx->n);
	if (!tree_size)
		free(ctx->t);
	if (DEBUGL(2))
		fprintf(stderr, "done freeing node at %s, tree size %lu\n", str, tree_size);
	free(str);
	free(ctx);
	return NULL;
}

/* Asynchronously free the subtree of nodes rooted at n. If the tree becomes
 * empty free the tree also.  Only for fast_alloc=false. */
static void
tree_done_node_detached(struct tree *t, struct tree_node *n)
{
	if (n->u.playouts < 1000) { // no thread for small tree
		if (!tree_done_node(t, n))
			free(t);
		return;
	}
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_t thread;
	struct subtree_ctx *ctx = malloc2(sizeof(struct subtree_ctx));
	ctx->t = t;
	ctx->n = n;
	pthread_create(&thread, &attr, tree_done_node_worker, ctx);
	pthread_attr_destroy(&attr);
}

void
tree_done(struct tree *t)
{
	tree_done_node(t, t->ltree_black);
	tree_done_node(t, t->ltree_white);

	if (t->htable) free(t->htable);
	if (t->nodes) {
		free(t->nodes);
		free(t);
	} else if (!tree_done_node(t, t->root)) {
		free(t);
		/* A tree_done_node_worker might still be running on this tree but
		 * it will free the tree later. It is also freeing nodes faster than
		 * we will create new ones. */
	}
}


static void
tree_node_dump(struct tree *tree, struct tree_node *node, int treeparity, int l, int thres)
{
	for (int i = 0; i < l; i++) fputc(' ', stderr);
	int children = 0;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		children++;
	/* We use 1 as parity, since for all nodes we want to know the
	 * win probability of _us_, not the node color. */
	fprintf(stderr, "[%s] %.3f/%d [prior %.3f/%d amaf %.3f/%d crit %.3f vloss %d] h=%x c#=%d <%"PRIhash">\n",
		coord2sstr(node_coord(node), tree->board),
		tree_node_get_value(tree, treeparity, node->u.value), node->u.playouts,
		tree_node_get_value(tree, treeparity, node->prior.value), node->prior.playouts,
		tree_node_get_value(tree, treeparity, node->amaf.value), node->amaf.playouts,
		tree_node_criticality(tree, node), node->descents,
		node->hints, children, node->hash);

	/* Print nodes sorted by #playouts. */

	struct tree_node *nbox[1000]; int nboxl = 0;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		if (ni->u.playouts > thres)
			nbox[nboxl++] = ni;

	while (true) {
		int best = -1;
		for (int i = 0; i < nboxl; i++)
			if (nbox[i] && (best < 0 || nbox[i]->u.playouts > nbox[best]->u.playouts))
				best = i;
		if (best < 0)
			break;
		tree_node_dump(tree, nbox[best], treeparity, l + 1, /* node->u.value < 0.1 ? 0 : */ thres);
		nbox[best] = NULL;
	}
}

void
tree_dump(struct tree *tree, double thres)
{
	int thres_abs = thres > 0 ? tree->root->u.playouts * thres : thres;
	fprintf(stderr, "(UCT tree; root %s; extra komi %f; max depth %d)\n",
	        stone2str(tree->root_color), tree->extra_komi,
		tree->max_depth - tree->root->depth);
	tree_node_dump(tree, tree->root, 1, 0, thres_abs);

	if (DEBUGL(3) && tree->ltree_black) {
		fprintf(stderr, "B local tree:\n");
		tree_node_dump(tree, tree->ltree_black, tree->root_color == S_WHITE ? 1 : -1, 0, thres_abs);
		fprintf(stderr, "W local tree:\n");
		tree_node_dump(tree, tree->ltree_white, tree->root_color == S_BLACK ? 1 : -1, 0, thres_abs);
	}
}


static char *
tree_book_name(struct board *b)
{
	static char buf[256];
	if (b->handicap > 0) {
		sprintf(buf, "ucttbook-%d-%02.01f-h%d.pachitree", b->size - 2, b->komi, b->handicap);
	} else {
		sprintf(buf, "ucttbook-%d-%02.01f.pachitree", b->size - 2, b->komi);
	}
	return buf;
}

static void
tree_node_save(FILE *f, struct tree_node *node, int thres)
{
	bool save_children = node->u.playouts >= thres;

	if (!save_children)
		node->is_expanded = 0;

	fputc(1, f);
	fwrite(((void *) node) + offsetof(struct tree_node, u),
	       sizeof(struct tree_node) - offsetof(struct tree_node, u),
	       1, f);

	if (save_children) {
		for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
			tree_node_save(f, ni, thres);
	} else {
		if (node->children)
			node->is_expanded = 1;
	}

	fputc(0, f);
}

void
tree_save(struct tree *tree, struct board *b, int thres)
{
	char *filename = tree_book_name(b);
	FILE *f = fopen(filename, "wb");
	if (!f) {
		perror("fopen");
		return;
	}
	tree_node_save(f, tree->root, thres);
	fputc(0, f);
	fclose(f);
}


void
tree_node_load(FILE *f, struct tree_node *node, int *num)
{
	(*num)++;

	fread(((void *) node) + offsetof(struct tree_node, u),
	      sizeof(struct tree_node) - offsetof(struct tree_node, u),
	      1, f);

	/* Keep values in sane scale, otherwise we start overflowing. */
#define MAX_PLAYOUTS	10000000
	if (node->u.playouts > MAX_PLAYOUTS) {
		node->u.playouts = MAX_PLAYOUTS;
	}
	if (node->amaf.playouts > MAX_PLAYOUTS) {
		node->amaf.playouts = MAX_PLAYOUTS;
	}
	memcpy(&node->pu, &node->u, sizeof(node->u));

	struct tree_node *ni = NULL, *ni_prev = NULL;
	while (fgetc(f)) {
		ni_prev = ni; ni = calloc2(1, sizeof(*ni));
		if (!node->children)
			node->children = ni;
		else
			ni_prev->sibling = ni;
		ni->parent = node;
		tree_node_load(f, ni, num);
	}
}

void
tree_load(struct tree *tree, struct board *b)
{
	char *filename = tree_book_name(b);
	FILE *f = fopen(filename, "rb");
	if (!f)
		return;

	fprintf(stderr, "Loading opening tbook %s...\n", filename);

	int num = 0;
	if (fgetc(f))
		tree_node_load(f, tree->root, &num);
	fprintf(stderr, "Loaded %d nodes.\n", num);

	fclose(f);
}


/* Copy the subtree rooted at node: all nodes at or below depth
 * or with at least threshold playouts. Only for fast_alloc.
 * The code is destructive on src. The relative order of children of
 * a given node is preserved (assumed by tree_get_node in particular).
 * Returns the copy of node in the destination tree, or NULL
 * if we could not copy it. */
static struct tree_node *
tree_prune(struct tree *dest, struct tree *src, struct tree_node *node,
	   int threshold, int depth)
{
	assert(dest->nodes && node);
	struct tree_node *n2 = tree_alloc_node(dest, 1, true);
	if (!n2)
		return NULL;
	*n2 = *node;
	if (n2->depth > dest->max_depth)
		dest->max_depth = n2->depth;
	n2->children = NULL;
	n2->is_expanded = false;

	if (node->depth >= depth && node->u.playouts < threshold)
		return n2;
	/* For deep nodes with many playouts, we must copy all children,
	 * even those with zero playouts, because partially expanded
	 * nodes are not supported. Considering them as fully expanded
	 * would degrade the playing strength. The only exception is
	 * when dest becomes full, but this should never happen in practice
	 * if threshold is chosen to limit the number of nodes traversed. */
	struct tree_node *ni = node->children;
	if (!ni)
		return n2;
	struct tree_node **prev2 = &(n2->children);
	while (ni) {
		struct tree_node *ni2 = tree_prune(dest, src, ni, threshold, depth);
		if (!ni2) break;
		*prev2 = ni2;
		prev2 = &(ni2->sibling);
		ni2->parent = n2;
		ni = ni->sibling;
	}
	if (!ni) {
		n2->is_expanded = true;
	} else {
		n2->children = NULL; // avoid partially expanded nodes
	}
	return n2;
}

/* The following constants are used for garbage collection of nodes.
 * A tree is considered large if the top node has >= 40K playouts.
 * For such trees, we copy deep nodes only if they have enough
 * playouts, with a gradually increasing threshold up to 40.
 * These constants define how much time we're willing to spend
 * scanning the source tree when promoting a move. The chosen values
 * make worst case pruning in about 3s for 20 GB ram, and this
 * is only for long thinking time (>1M playouts). For fast games the
 * trees don't grow large. For small ram or fast game we copy the
 * entire tree.  These values do not degrade playing strength and are
 * necessary to avoid losing on time; increasing DEEP_PLAYOUTS_THRESHOLD
 * or decreasing LARGE_TREE_PLAYOUTS will make the program faster but
 * playing worse. */
#define LARGE_TREE_PLAYOUTS 40000LL
#define DEEP_PLAYOUTS_THRESHOLD 40

/* Garbage collect the tree early if the top node has < 5K playouts,
 * to avoid having to do it later on a large subtree.
 * This guarantees garbage collection in < 1s. */
#define SMALL_TREE_PLAYOUTS 5000

/* Free all the tree, keeping only the subtree rooted at node.
 * Prune the subtree if necessary to fit in memory or
 * to save time scanning the tree.
 * Returns the moved node. Only for fast_alloc. */
struct tree_node *
tree_garbage_collect(struct tree *tree, struct tree_node *node)
{
	assert(tree->nodes && !node->parent && !node->sibling);
	double start_time = time_now();
	unsigned long orig_size = tree->nodes_size;

	struct tree *temp_tree = tree_init(tree->board,  tree->root_color,
					   tree->max_pruned_size, 0, 0, tree->ltree_aging, 0);
	temp_tree->nodes_size = 0; // We do not want the dummy pass node
        struct tree_node *temp_node;

	/* Find the maximum depth at which we can copy all nodes. */
	int max_nodes = 1;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		max_nodes++;
	unsigned long nodes_size = max_nodes * sizeof(*node);
	int max_depth = node->depth;
	while (nodes_size < tree->max_pruned_size && max_nodes > 1) {
		max_nodes--;
		nodes_size += max_nodes * nodes_size;
		max_depth++;
	}

	/* Copy all nodes for small trees. For large trees, copy all nodes
	 * with depth <= max_depth, and all nodes with enough playouts.
	 * Avoiding going too deep (except for nodes with many playouts) is mostly
	 * to save time scanning the source tree. It can take over 20s to traverse
	 * completely a large source tree (20 GB) even without copying because
	 * the traversal is not friendly at all with the memory cache. */
	int threshold = (node->u.playouts - LARGE_TREE_PLAYOUTS) * DEEP_PLAYOUTS_THRESHOLD / LARGE_TREE_PLAYOUTS;
	if (threshold < 0) threshold = 0;
	if (threshold > DEEP_PLAYOUTS_THRESHOLD) threshold = DEEP_PLAYOUTS_THRESHOLD; 
	temp_node = tree_prune(temp_tree, tree, node, threshold, max_depth);
	assert(temp_node);

	/* Now copy back to original tree. */
	tree->nodes_size = 0;
	tree->max_depth = 0;
	struct tree_node *new_node = tree_prune(tree, temp_tree, temp_node, 0, temp_tree->max_depth);

	if (DEBUGL(1)) {
		double now = time_now();
		static double prev_time;
		if (!prev_time) prev_time = start_time;
		fprintf(stderr,
			"tree pruned in %0.6g s, prev %0.3g s ago, dest depth %d wanted %d,"
			" size %lu->%lu/%lu, playouts %d\n",
			now - start_time, start_time - prev_time, temp_tree->max_depth, max_depth,
			orig_size, temp_tree->nodes_size, tree->max_pruned_size, new_node->u.playouts);
		prev_time = start_time;
	}
	if (temp_tree->nodes_size >= temp_tree->max_tree_size) {
		fprintf(stderr, "temp tree overflow, max_tree_size %lu, pruning_threshold %lu\n",
			tree->max_tree_size, tree->pruning_threshold);
		/* This is not a serious problem, we will simply recompute the discarded nodes
		 * at the next move if necessary. This is better than frequently wasting memory. */
	} else {
		assert(tree->nodes_size == temp_tree->nodes_size);
		assert(tree->max_depth == temp_tree->max_depth);
	}
	tree_done(temp_tree);
	return new_node;
}


/* Get a node of given coordinate from within parent, possibly creating it
 * if necessary - in a very raw form (no .d, priors, ...). */
/* FIXME: Adjust for board symmetry. */
struct tree_node *
tree_get_node(struct tree *t, struct tree_node *parent, coord_t c, bool create)
{
	if (!parent->children || node_coord(parent->children) >= c) {
		/* Special case: Insertion at the beginning. */
		if (parent->children && node_coord(parent->children) == c)
			return parent->children;
		if (!create)
			return NULL;

		struct tree_node *nn = tree_init_node(t, c, parent->depth + 1, false);
		nn->parent = parent; nn->sibling = parent->children;
		parent->children = nn;
		return nn;
	}

	/* No candidate at the beginning, look through all the children. */

	struct tree_node *ni;
	for (ni = parent->children; ni->sibling; ni = ni->sibling)
		if (node_coord(ni->sibling) >= c)
			break;

	if (ni->sibling && node_coord(ni->sibling) == c)
		return ni->sibling;
	assert(node_coord(ni) < c);
	if (!create)
		return NULL;

	struct tree_node *nn = tree_init_node(t, c, parent->depth + 1, false);
	nn->parent = parent; nn->sibling = ni->sibling; ni->sibling = nn;
	return nn;
}

/* Get local tree node corresponding to given node, given local node child
 * iterator @lni (which points either at the corresponding node, or at the
 * nearest local tree node after @ni). */
struct tree_node *
tree_lnode_for_node(struct tree *tree, struct tree_node *ni, struct tree_node *lni, int tenuki_d)
{
	/* Now set up lnode, which is the actual local node
	 * corresponding to ni - either lni if it is an
	 * exact match and ni is not tenuki, <pass> local
	 * node if ni is tenuki, or NULL if there is no
	 * corresponding node available. */

	if (is_pass(node_coord(ni))) {
		/* Also, for sanity reasons we never use local
		 * tree for passes. (Maybe we could, but it's
		 * too hard to think about.) */
		return NULL;
	}

	if (node_coord(lni) == node_coord(ni)) {
		/* We don't consider tenuki a sequence play
		 * that we have in local tree even though
		 * ni->d is too high; this can happen if this
		 * occured in different board topology. */
		return lni;
	}

	if (ni->d >= tenuki_d) {
		/* Tenuki, pick a pass lsibling if available. */
		assert(lni->parent && lni->parent->children);
		if (is_pass(node_coord(lni->parent->children))) {
			return lni->parent->children;
		} else {
			return NULL;
		}
	}

	/* No corresponding local node, lnode stays NULL. */
	return NULL;
}


/* Tree symmetry: When possible, we will localize the tree to a single part
 * of the board in tree_expand_node() and possibly flip along symmetry axes
 * to another part of the board in tree_promote_at(). We follow b->symmetry
 * guidelines here. */


/* This function must be thread safe, given that board b is only modified by the calling thread. */
void
tree_expand_node(struct tree *t, struct tree_node *node, struct board *b, enum stone color, struct uct *u, int parity)
{
	/* Get a Common Fate Graph distance map from parent node. */
	int distances[board_size2(b)];
	if (!is_pass(b->last_move.coord) && !is_resign(b->last_move.coord)) {
		cfg_distances(b, node_coord(node), distances, TREE_NODE_D_MAX);
	} else {
		// Pass or resign - everything is too far.
		foreach_point(b) { distances[c] = TREE_NODE_D_MAX + 1; } foreach_point_end;
	}

	/* Get a map of prior values to initialize the new nodes with. */
	struct prior_map map = {
		.b = b,
		.to_play = color,
		.parity = tree_parity(t, parity),
		.distances = distances,
	};
	// Include pass in the prior map.
	struct move_stats map_prior[board_size2(b) + 1]; map.prior = &map_prior[1];
	bool map_consider[board_size2(b) + 1]; map.consider = &map_consider[1];
	memset(map_prior, 0, sizeof(map_prior));
	memset(map_consider, 0, sizeof(map_consider));
	map.consider[pass] = true;
	int child_count = 1; // for pass
	foreach_free_point(b) {
		assert(board_at(b, c) == S_NONE);
		if (!board_is_valid_play_no_suicide(b, color, c))
			continue;
		map.consider[c] = true;
		child_count++;
	} foreach_free_point_end;
	uct_prior(u, node, &map);

	/* Now, create the nodes (all at once if fast_alloc) */
	struct tree_node *ni = t->nodes ? tree_alloc_node(t, child_count, true) : tree_alloc_node(t, 1, false);
	/* In fast_alloc mode we might temporarily run out of nodes but this should be rare. */
	if (!ni) {
		node->is_expanded = false;
		return;
	}
	tree_setup_node(t, ni, pass, node->depth + 1);

	struct tree_node *first_child = ni;
	ni->parent = node;
	ni->prior = map.prior[pass]; ni->d = TREE_NODE_D_MAX + 1;

	/* The loop considers only the symmetry playground. */
	if (UDEBUGL(6)) {
		fprintf(stderr, "expanding %s within [%d,%d],[%d,%d] %d-%d\n",
				coord2sstr(node_coord(node), b),
				b->symmetry.x1, b->symmetry.y1,
				b->symmetry.x2, b->symmetry.y2,
				b->symmetry.type, b->symmetry.d);
	}
	int child = 1;
	for (int j = b->symmetry.y1; j <= b->symmetry.y2; j++) {
		for (int i = b->symmetry.x1; i <= b->symmetry.x2; i++) {
			if (b->symmetry.d) {
				int x = b->symmetry.type == SYM_DIAG_DOWN ? board_size(b) - 1 - i : i;
				if (x > j) {
					if (UDEBUGL(7))
						fprintf(stderr, "drop %d,%d\n", i, j);
					continue;
				}
			}

			coord_t c = coord_xy(t->board, i, j);
			if (!map.consider[c]) // Filter out invalid moves
				continue;
			assert(c != node_coord(node)); // I have spotted "C3 C3" in some sequence...

			struct tree_node *nj = t->nodes ? first_child + child++ : tree_alloc_node(t, 1, false);
			tree_setup_node(t, nj, c, node->depth + 1);
			nj->parent = node; ni->sibling = nj; ni = nj;

			ni->prior = map.prior[c];
			ni->d = distances[c];
		}
	}
	node->children = first_child; // must be done at the end to avoid race
}


static coord_t
flip_coord(struct board *b, coord_t c,
           bool flip_horiz, bool flip_vert, int flip_diag)
{
	int x = coord_x(c, b), y = coord_y(c, b);
	if (flip_diag) {
		int z = x; x = y; y = z;
	}
	if (flip_horiz) {
		x = board_size(b) - 1 - x;
	}
	if (flip_vert) {
		y = board_size(b) - 1 - y;
	}
	return coord_xy(b, x, y);
}

static void
tree_fix_node_symmetry(struct board *b, struct tree_node *node,
                       bool flip_horiz, bool flip_vert, int flip_diag)
{
	if (!is_pass(node_coord(node)))
		node->coord = flip_coord(b, node_coord(node), flip_horiz, flip_vert, flip_diag);

	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		tree_fix_node_symmetry(b, ni, flip_horiz, flip_vert, flip_diag);
}

static void
tree_fix_symmetry(struct tree *tree, struct board *b, coord_t c)
{
	if (is_pass(c))
		return;

	struct board_symmetry *s = &tree->root_symmetry;
	int cx = coord_x(c, b), cy = coord_y(c, b);

	/* playground	X->h->v->d normalization
	 * :::..	.d...
	 * .::..	v....
	 * ..:..	.....
	 * .....	h...X
	 * .....	.....  */
	bool flip_horiz = cx < s->x1 || cx > s->x2;
	bool flip_vert = cy < s->y1 || cy > s->y2;

	bool flip_diag = 0;
	if (s->d) {
		bool dir = (s->type == SYM_DIAG_DOWN);
		int x = dir ^ flip_horiz ^ flip_vert ? board_size(b) - 1 - cx : cx;
		if (flip_vert ? x < cy : x > cy) {
			flip_diag = 1;
		}
	}

	if (DEBUGL(4)) {
		fprintf(stderr, "%s [%d,%d -> %d,%d;%d,%d] will flip %d %d %d -> %s, sym %d (%d) -> %d (%d)\n",
			coord2sstr(c, b),
			cx, cy, s->x1, s->y1, s->x2, s->y2,
			flip_horiz, flip_vert, flip_diag,
			coord2sstr(flip_coord(b, c, flip_horiz, flip_vert, flip_diag), b),
			s->type, s->d, b->symmetry.type, b->symmetry.d);
	}
	if (flip_horiz || flip_vert || flip_diag)
		tree_fix_node_symmetry(b, tree->root, flip_horiz, flip_vert, flip_diag);
}


static void
tree_unlink_node(struct tree_node *node)
{
	struct tree_node *ni = node->parent;
	if (ni->children == node) {
		ni->children = node->sibling;
	} else {
		ni = ni->children;
		while (ni->sibling != node)
			ni = ni->sibling;
		ni->sibling = node->sibling;
	}
	node->sibling = NULL;
	node->parent = NULL;
}

/* Reduce weight of statistics on promotion. Remove nodes that
 * get reduced to zero playouts; returns next node to consider
 * in the children list (@node may get deleted). */
static struct tree_node *
tree_age_node(struct tree *tree, struct tree_node *node)
{
	node->u.playouts /= tree->ltree_aging;
	if (node->parent && !node->u.playouts) {
		struct tree_node *sibling = node->sibling;
		/* Delete node, no playouts. */
		tree_unlink_node(node);
		tree_done_node(tree, node);
		return sibling;
	}

	struct tree_node *ni = node->children;
	while (ni) ni = tree_age_node(tree, ni);
	return node->sibling;
}

/* Promotes the given node as the root of the tree. In the fast_alloc
 * mode, the node may be moved and some of its subtree may be pruned. */
void
tree_promote_node(struct tree *tree, struct tree_node **node)
{
	assert((*node)->parent == tree->root);
	tree_unlink_node(*node);
	if (!tree->nodes) {
		/* Freeing the rest of the tree can take several seconds on large
		 * trees, so we must do it asynchronously: */
		tree_done_node_detached(tree, tree->root);
	} else {
		/* Garbage collect if we run out of memory, or it is cheap to do so now: */
		if (tree->nodes_size >= tree->pruning_threshold
		    || (tree->nodes_size >= tree->max_tree_size / 10 && (*node)->u.playouts < SMALL_TREE_PLAYOUTS))
			*node = tree_garbage_collect(tree, *node);
	}
	tree->root = *node;
	tree->root_color = stone_other(tree->root_color);

	board_symmetry_update(tree->board, &tree->root_symmetry, node_coord(*node));
	tree->avg_score.playouts = 0;

	/* If the tree deepest node was under node, or if we called tree_garbage_collect,
	 * tree->max_depth is correct. Otherwise we could traverse the tree
         * to recompute max_depth but it's not worth it: it's just for debugging
	 * and soon the tree will grow and max_depth will become correct again. */

	if (tree->ltree_aging != 1.0f) { // XXX: != should work here even with the floating_t
		tree_age_node(tree, tree->ltree_black);
		tree_age_node(tree, tree->ltree_white);
	}
}

bool
tree_promote_at(struct tree *tree, struct board *b, coord_t c)
{
	tree_fix_symmetry(tree, b, c);

	for (struct tree_node *ni = tree->root->children; ni; ni = ni->sibling) {
		if (node_coord(ni) == c) {
			tree_promote_node(tree, &ni);
			return true;
		}
	}
	return false;
}
