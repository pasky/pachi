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
#include "dcnn.h"


/* Allocate tree node(s). The returned nodes are initialized with zeroes.
 * Returns NULL if not enough memory.
 * This function may be called by multiple threads in parallel. */
static tree_node_t *
tree_alloc_node(tree_t *t, int count, bool fast_alloc)
{
	tree_node_t *n = NULL;
	size_t nsize = count * sizeof(*n);
	size_t old_size = __sync_fetch_and_add(&t->nodes_size, nsize);

	if (fast_alloc) {
		if (old_size + nsize > t->max_tree_size)
			return NULL;
		assert(t->nodes != NULL);
		n = (tree_node_t *)((char*)t->nodes + old_size);
		memset(n, 0, nsize);
	} else {
		n = calloc2(count, tree_node_t);
	}
	return n;
}

/* Initialize a node at a given place in memory.
 * This function may be called by multiple threads in parallel. */
static void
tree_setup_node(tree_t *t, tree_node_t *n, coord_t coord, int depth)
{
	static volatile unsigned int hash = 0;
	n->coord = coord;
	n->depth = depth;
	/* n->hash is used only for debugging. It is very likely (but not
	 * guaranteed) to be unique. */
	hash_t h = n - (tree_node_t *)0;
	n->hash = (h << 32) + (hash++ & 0xffffffff);
	if (depth > t->max_depth)
		t->max_depth = depth;
}

/* Allocate and initialize a node. Returns NULL (fast_alloc mode)
 * or exits the main program if not enough memory.
 * This function may be called by multiple threads in parallel. */
static tree_node_t *
tree_init_node(tree_t *t, coord_t coord, int depth, bool fast_alloc)
{
	tree_node_t *n;
	n = tree_alloc_node(t, 1, fast_alloc);
	if (!n) return NULL;
	tree_setup_node(t, n, coord, depth);
	return n;
}

/* Create a tree structure. Pre-allocate all nodes if max_tree_size is > 0. */
tree_t *
tree_init(board_t *board, enum stone color, size_t max_tree_size,
	  size_t max_pruned_size, size_t pruning_threshold, floating_t ltree_aging, int hbits)
{
	tree_t *t = calloc2(1, tree_t);
	t->board = board;
	t->max_tree_size = max_tree_size;
	t->max_pruned_size = max_pruned_size;
	t->pruning_threshold = pruning_threshold;
	if (max_tree_size != 0) {
		if (DEBUGL(3)) fprintf(stderr, "allocating %i Mb for search tree\n", max_tree_size / (1024*1024));
		t->nodes = cmalloc(max_tree_size);
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
static size_t
tree_done_node(tree_t *t, tree_node_t *n)
{
	tree_node_t *ni = n->children;
	while (ni) {
		tree_node_t *nj = ni->sibling;
		tree_done_node(t, ni);
		ni = nj;
	}
	free(n);
	size_t old_size = __sync_fetch_and_sub(&t->nodes_size, sizeof(*n));
	return old_size - sizeof(*n);
}

typedef struct {
	tree_t *t;
	tree_node_t *n;
} subtree_ctx_t;

/* Worker thread for tree_done_node_detached(). Only for fast_alloc=false. */
static void *
tree_done_node_worker(void *ctx_)
{
	subtree_ctx_t *ctx = (subtree_ctx_t*)ctx_;
	char *str = coord2str(node_coord(ctx->n));

	size_t tree_size = tree_done_node(ctx->t, ctx->n);
	if (!tree_size)
		free(ctx->t);
	if (DEBUGL(2))
		fprintf(stderr, "done freeing node at %s, tree size %llu\n", str, (unsigned long long)tree_size);
	free(str);
	free(ctx);
	return NULL;
}

/* Asynchronously free the subtree of nodes rooted at n. If the tree becomes
 * empty free the tree also.  Only for fast_alloc=false. */
static void
tree_done_node_detached(tree_t *t, tree_node_t *n)
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
	subtree_ctx_t *ctx = malloc2(subtree_ctx_t);
	ctx->t = t;
	ctx->n = n;
	pthread_create(&thread, &attr, tree_done_node_worker, ctx);
	pthread_attr_destroy(&attr);
}

void
tree_done(tree_t *t)
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
tree_node_dump(tree_t *tree, tree_node_t *node, int treeparity, int l, int thres)
{
	for (int i = 0; i < l; i++) fputc(' ', stderr);
	int children = 0;
	for (tree_node_t *ni = node->children; ni; ni = ni->sibling)
		children++;
	/* We use 1 as parity, since for all nodes we want to know the
	 * win probability of _us_, not the node color. */
	fprintf(stderr, "[%s] %.3f/%d [prior %.3f/%d amaf %.3f/%d crit %.3f vloss %d] h=%x c#=%d <%" PRIhash ">\n",
		coord2sstr(node_coord(node)),
		tree_node_get_value(tree, treeparity, node->u.value), node->u.playouts,
		tree_node_get_value(tree, treeparity, node->prior.value), node->prior.playouts,
		tree_node_get_value(tree, treeparity, node->amaf.value), node->amaf.playouts,
		tree_node_criticality(tree, node), node->descents,
		node->hints, children, node->hash);

	/* Print nodes sorted by #playouts. */

	tree_node_t *nbox[1000]; int nboxl = 0;
	for (tree_node_t *ni = node->children; ni; ni = ni->sibling)
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
tree_dump(tree_t *tree, double thres)
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
tree_book_name(board_t *b)
{
	int size = board_rsize(b);
	static char buf[256];
	if (b->handicap > 0)
		sprintf(buf, "ucttbook-%d-%02.01f-h%d.pachitree", size, b->komi, b->handicap);
	else
		sprintf(buf, "ucttbook-%d-%02.01f.pachitree", size, b->komi);
	return buf;
}

static void
tree_node_save(FILE *f, tree_node_t *node, int thres)
{
	bool save_children = node->u.playouts >= thres;

	if (!save_children)
		node->is_expanded = 0;

	fputc(1, f);
	fwrite(((int *) node) + offsetof(tree_node_t, u),
	       sizeof(tree_node_t) - offsetof(tree_node_t, u),
	       1, f);

	if (save_children) {
		for (tree_node_t *ni = node->children; ni; ni = ni->sibling)
			tree_node_save(f, ni, thres);
	} else {
		if (node->children)
			node->is_expanded = 1;
	}

	fputc(0, f);
}

void
tree_save(tree_t *tree, board_t *b, int thres)
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
tree_node_load(FILE *f, tree_node_t *node, int *num)
{
	(*num)++;

	checked_fread(((int *) node) + offsetof(tree_node_t, u),
		      sizeof(tree_node_t) - offsetof(tree_node_t, u),
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

	tree_node_t *ni = NULL, *ni_prev = NULL;
	while (fgetc(f)) {
		ni_prev = ni; ni = calloc2(1, tree_node_t);
		if (!node->children)
			node->children = ni;
		else
			ni_prev->sibling = ni;
		ni->parent = node;
		tree_node_load(f, ni, num);
	}
}

void
tree_load(tree_t *tree, board_t *b)
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
static tree_node_t *
tree_prune(tree_t *dest, tree_t *src, tree_node_t *node,
	   int threshold, int depth)
{
	assert(dest->nodes && node);
	tree_node_t *n2 = tree_alloc_node(dest, 1, true);
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
	tree_node_t *ni = node->children;
	if (!ni)
		return n2;
	tree_node_t **prev2 = &(n2->children);
	while (ni) {
		tree_node_t *ni2 = tree_prune(dest, src, ni, threshold, depth);
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
tree_node_t *
tree_garbage_collect(tree_t *tree, tree_node_t *node)
{
	assert(tree->nodes && !node->parent && !node->sibling);
	double start_time = time_now();
	size_t orig_size = tree->nodes_size;

	tree_t *temp_tree = tree_init(tree->board,  tree->root_color,
					   tree->max_pruned_size, 0, 0, tree->ltree_aging, 0);
	temp_tree->nodes_size = 0; // We do not want the dummy pass node
        tree_node_t *temp_node;

	/* Find the maximum depth at which we can copy all nodes. */
	int max_nodes = 1;
	for (tree_node_t *ni = node->children; ni; ni = ni->sibling)
		max_nodes++;
	size_t nodes_size = max_nodes * sizeof(*node);
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
	tree_node_t *new_node = tree_prune(tree, temp_tree, temp_node, 0, temp_tree->max_depth);

	if (DEBUGL(1)) {
		double now = time_now();
		static double prev_time;
		if (!prev_time) prev_time = start_time;
		fprintf(stderr,
			"tree pruned in %0.3fs, prev %0.1fs ago, dest depth %d wanted %d,"
			" size %llu->%llu/%llu, playouts %d\n",
			now - start_time, start_time - prev_time, temp_tree->max_depth, max_depth,
			(unsigned long long)orig_size, (unsigned long long)temp_tree->nodes_size, (unsigned long long)tree->max_pruned_size, new_node->u.playouts);
		prev_time = start_time;
	}
	if (temp_tree->nodes_size >= temp_tree->max_tree_size) {
		fprintf(stderr, "temp tree overflow, max_tree_size %llu, pruning_threshold %llu\n",
			(unsigned long long)tree->max_tree_size, (unsigned long long)tree->pruning_threshold);
		/* This is not a serious problem, we will simply recompute the discarded nodes
		 * at the next move if necessary. This is better than frequently wasting memory. */
	} else {
		assert(tree->nodes_size == temp_tree->nodes_size);
		assert(tree->max_depth == temp_tree->max_depth);
	}
	tree_done(temp_tree);
	return new_node;
}

void
tree_copy(tree_t *dst, tree_t *src)
{
	dst->nodes_size = 0;
	dst->max_depth = 0;
	// just copy everything for now ...
	dst->root = tree_prune(dst, src, src->root, 0, src->max_depth);
	assert(dst->root);
}

void
tree_realloc(tree_t *t, size_t max_tree_size, size_t max_pruned_size, size_t pruning_threshold)
{
	assert(max_tree_size > t->max_tree_size);
	assert(max_pruned_size > t->max_pruned_size);
	assert(pruning_threshold > t->pruning_threshold);

	tree_t *t2 = tree_init(t->board, stone_other(t->root_color), max_tree_size, max_pruned_size,
			       pruning_threshold, t->ltree_aging, t->hbits);

	tree_copy(t2, t);   assert(t2->root_color == t->root_color);

	tree_t *tmp = malloc2(tree_t);
	*tmp = *t;  tree_done(tmp);
	*t = *t2;   free(t2);
}


/* Find node of given coordinate under parent.
 * FIXME: Adjust for board symmetry. */
tree_node_t *
tree_get_node(tree_node_t *parent, coord_t c)
{
	for (tree_node_t *n = parent->children; n; n = n->sibling)
		if (node_coord(n) == c)
			return n;
	return NULL;
}

/* Get a node of given coordinate from within parent, possibly creating it
 * if necessary - in a very raw form (no .d, priors, ...). */
/* FIXME: Adjust for board symmetry. */
tree_node_t *
tree_get_node2(tree_t *t, tree_node_t *parent, coord_t c, bool create)
{
	if (!parent->children || node_coord(parent->children) >= c) {
		/* Special case: Insertion at the beginning. */
		if (parent->children && node_coord(parent->children) == c)
			return parent->children;
		if (!create)
			return NULL;

		tree_node_t *nn = tree_init_node(t, c, parent->depth + 1, false);
		nn->parent = parent; nn->sibling = parent->children;
		parent->children = nn;
		return nn;
	}

	/* No candidate at the beginning, look through all the children. */

	tree_node_t *ni;
	for (ni = parent->children; ni->sibling; ni = ni->sibling)
		if (node_coord(ni->sibling) >= c)
			break;

	if (ni->sibling && node_coord(ni->sibling) == c)
		return ni->sibling;
	assert(node_coord(ni) < c);
	if (!create)
		return NULL;

	tree_node_t *nn = tree_init_node(t, c, parent->depth + 1, false);
	nn->parent = parent; nn->sibling = ni->sibling; ni->sibling = nn;
	return nn;
}

/* Get local tree node corresponding to given node, given local node child
 * iterator @lni (which points either at the corresponding node, or at the
 * nearest local tree node after @ni). */
tree_node_t *
tree_lnode_for_node(tree_t *tree, tree_node_t *ni, tree_node_t *lni, int tenuki_d)
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
tree_expand_node(tree_t *t, tree_node_t *node, board_t *b, enum stone color, uct_t *u, int parity)
{
	/* Get a Common Fate Graph distance map from parent node. */
	int distances[board_max_coords(b)];
	if (!is_pass(last_move(b).coord))
		cfg_distances(b, last_move(b).coord, distances, TREE_NODE_D_MAX);
	else    // Pass - everything is too far.
		foreach_point(b) { distances[c] = TREE_NODE_D_MAX + 1; } foreach_point_end;

	/* Include pass in the prior map. */
	move_stats_t map_prior[board_max_coords(b) + 1];      memset(map_prior, 0, sizeof(map_prior));
	bool         map_consider[board_max_coords(b) + 1];   memset(map_consider, 0, sizeof(map_consider));
	
	/* Get a map of prior values to initialize the new nodes with. */
	prior_map_t map = { b, color, tree_parity(t, parity), &map_prior[1], &map_consider[1], distances };
	
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
	tree_node_t *ni = t->nodes ? tree_alloc_node(t, child_count, true) : tree_alloc_node(t, 1, false);
	/* In fast_alloc mode we might temporarily run out of nodes but this should be rare. */
	if (!ni) {
		node->is_expanded = false;
		return;
	}
	tree_setup_node(t, ni, pass, node->depth + 1);

	tree_node_t *first_child = ni;
	ni->parent = node;
	ni->prior = map.prior[pass]; ni->d = TREE_NODE_D_MAX + 1;

	/* The loop considers only the symmetry playground. */
	if (UDEBUGL(6)) {
		fprintf(stderr, "expanding %s within [%d,%d],[%d,%d] %d-%d\n",
				coord2sstr(node_coord(node)),
				b->symmetry.x1, b->symmetry.y1,
				b->symmetry.x2, b->symmetry.y2,
				b->symmetry.type, b->symmetry.d);
	}
	int child = 1;
	for (int j = b->symmetry.y1; j <= b->symmetry.y2; j++) {
		for (int i = b->symmetry.x1; i <= b->symmetry.x2; i++) {
			if (b->symmetry.d) {
				int x = b->symmetry.type == SYM_DIAG_DOWN ? board_stride(b) - 1 - i : i;
				if (x > j) {
					if (UDEBUGL(7))
						fprintf(stderr, "drop %d,%d\n", i, j);
					continue;
				}
			}

			coord_t c = coord_xy(i, j);
			if (!map.consider[c]) // Filter out invalid moves
				continue;
			assert(c != node_coord(node)); // I have spotted "C3 C3" in some sequence...

			tree_node_t *nj = t->nodes ? first_child + child++ : tree_alloc_node(t, 1, false);
			tree_setup_node(t, nj, c, node->depth + 1);
			nj->parent = node; ni->sibling = nj; ni = nj;

			ni->prior = map.prior[c];
			ni->d = distances[c];
		}
	}
	node->children = first_child; // must be done at the end to avoid race
}


static coord_t
flip_coord(board_t *b, coord_t c,
           bool flip_horiz, bool flip_vert, int flip_diag)
{
	int x = coord_x(c), y = coord_y(c);
	if (flip_diag)  {  int z = x; x = y; y = z;    }
	if (flip_horiz) {  x = board_stride(b) - 1 - x;  }
	if (flip_vert)  {  y = board_stride(b) - 1 - y;  }
	return coord_xy(x, y);
}

static void
tree_fix_node_symmetry(board_t *b, tree_node_t *node,
                       bool flip_horiz, bool flip_vert, int flip_diag)
{
	if (!is_pass(node_coord(node)))
		node->coord = flip_coord(b, node_coord(node), flip_horiz, flip_vert, flip_diag);

	for (tree_node_t *ni = node->children; ni; ni = ni->sibling)
		tree_fix_node_symmetry(b, ni, flip_horiz, flip_vert, flip_diag);
}

static void
tree_fix_symmetry(tree_t *tree, board_t *b, coord_t c)
{
	if (is_pass(c))
		return;

	board_symmetry_t *s = &tree->root_symmetry;
	int cx = coord_x(c), cy = coord_y(c);

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
		int x = dir ^ flip_horiz ^ flip_vert ? board_stride(b) - 1 - cx : cx;
		if (flip_vert ? x < cy : x > cy) {
			flip_diag = 1;
		}
	}

	if (DEBUGL(4)) {
		fprintf(stderr, "%s [%d,%d -> %d,%d;%d,%d] will flip %d %d %d -> %s, sym %d (%d) -> %d (%d)\n",
			coord2sstr(c),
			cx, cy, s->x1, s->y1, s->x2, s->y2,
			flip_horiz, flip_vert, flip_diag,
			coord2sstr(flip_coord(b, c, flip_horiz, flip_vert, flip_diag)),
			s->type, s->d, b->symmetry.type, b->symmetry.d);
	}
	if (flip_horiz || flip_vert || flip_diag)
		tree_fix_node_symmetry(b, tree->root, flip_horiz, flip_vert, flip_diag);
}


static void
tree_unlink_node(tree_node_t *node)
{
	tree_node_t *ni = node->parent;
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
static tree_node_t *
tree_age_node(tree_t *tree, tree_node_t *node)
{
	node->u.playouts /= tree->ltree_aging;
	if (node->parent && !node->u.playouts) {
		tree_node_t *sibling = node->sibling;
		/* Delete node, no playouts. */
		tree_unlink_node(node);
		tree_done_node(tree, node);
		return sibling;
	}

	tree_node_t *ni = node->children;
	while (ni) ni = tree_age_node(tree, ni);
	return node->sibling;
}

/* Promotes the given node as the root of the tree. In the fast_alloc
 * mode, the node may be moved and some of its subtree may be pruned. */
void
tree_promote_node(tree_t *tree, tree_node_t **node)
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
tree_promote_at(tree_t *t, board_t *b, coord_t c, int *reason)
{
	*reason = 0;
	tree_fix_symmetry(t, b, c);

	tree_node_t *n = tree_get_node(t->root, c);
	if (!n)  return false;
	
	if (using_dcnn(b) && !(n->hints & TREE_HINT_DCNN)) {
		*reason = TREE_HINT_DCNN;
		return false;  /* No dcnn priors, can't reuse ... */
	}
	
	tree_promote_node(t, &n);
	return true;
}
