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
#include "dcnn.h"

#ifdef DISTRIBUTED
#include "uct/slave.h"
#endif


/* Allocate tree node(s). The returned nodes are initialized with zeroes.
 * Returns NULL if not enough memory.
 * This function may be called by multiple threads in parallel.
 * Beware tree nodes_size increases even when alloc fails.
 * That's fine, used to detect tree memory full and will be fixed
 * at next tree garbage collect. */
static tree_node_t *
tree_alloc_node(tree_t *t, int count)
{
	tree_node_t *n = NULL;
	size_t nsize = count * sizeof(*n);
	size_t old_size = __sync_fetch_and_add(&t->nodes_size, nsize);

	if (old_size + nsize > t->max_tree_size)
		return NULL;  /* Not reverting nodes_size, see above */
	assert(t->nodes != NULL);
	n = (tree_node_t *)((char*)t->nodes + old_size);
	memset(n, 0, nsize);
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

/* Allocate and initialize a node. Returns NULL if tree memory is full.
 * This function may be called by multiple threads in parallel. */
static tree_node_t *
tree_init_node(tree_t *t, coord_t coord, int depth)
{
	tree_node_t *n;
	n = tree_alloc_node(t, 1);
	if (!n) return NULL;
	tree_setup_node(t, n, coord, depth);
	return n;
}

/* Create a tree structure and pre-allocate all nodes.
 * Returns NULL if out of memory */
tree_t *
tree_init(board_t *board, enum stone color, size_t max_tree_size,
	  size_t max_pruned_size, size_t pruning_threshold, int hbits)
{
	tree_node_t *nodes = NULL;
	assert (max_tree_size != 0);
	
	/* The nodes buffer doesn't need initialization. This is currently
	 * done by tree_init_node to spread the load. Doing a memset for the
	 * entire buffer here would be too slow for large trees (>10 GB). */
	if (!(nodes = malloc(max_tree_size))) {
		if (DEBUGL(2))  fprintf(stderr, "Out of memory.\n");
		return NULL;
	}
	
	tree_t *t = calloc2(1, tree_t);
	t->board = board;
	t->max_tree_size = max_tree_size;
	t->max_pruned_size = max_pruned_size;
	t->pruning_threshold = pruning_threshold;
	t->nodes = nodes;
	/* The root PASS move is only virtual, we never play it. */
	t->root = tree_init_node(t, pass, 0);
	t->root_color = stone_other(color); // to research black moves, root will be white

#ifdef DISTRIBUTED
	t->hbits = hbits;
	if (hbits) t->htable = uct_htable_alloc(hbits);
#endif
	return t;
}

void
tree_done(tree_t *t)
{
#ifdef DISTRIBUTED
	if (t->htable) free(t->htable);
#endif
	assert(t->nodes);
	free(t->nodes);
	free(t);
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

static tree_node_t *
tree_prune_node(tree_t *dest, tree_t *src, tree_node_t *node,
		int threshold, int depth)
{
	assert(dest->nodes && node);
	tree_node_t *n2 = tree_alloc_node(dest, 1);
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
		tree_node_t *ni2 = tree_prune_node(dest, src, ni, threshold, depth);
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

/* Prune src tree into dest (nodes are copied).
 * Keep all nodes at or below depth with at least threshold playouts.
 * The relative order of children of a given node is preserved
 * (assumed by tree_get_node() in particular).
 * Note: Only for fast_alloc. */
static void
tree_prune(tree_t *dest, tree_t *src, int threshold, int depth)
{
        tree_node_t *node = src->root;
	assert(dest->nodes && node);

	dest->use_extra_komi = src->use_extra_komi;
	dest->untrustworthy_tree = src->untrustworthy_tree;
	dest->extra_komi = src->extra_komi;
	dest->avg_score = src->avg_score;
	/* DISTRIBUTED htable not copied, gets rebuilt as needed */
	dest->nodes_size = 0;	/* we do not want the dummy pass node */
	dest->max_depth = 0;	/* gets recomputed */
	dest->root_color = src->root_color;
	dest->root = tree_prune_node(dest, src, node, threshold, depth);
	assert(dest->root);	
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

/* Tree garbage collection
 * Right now this does 3 things:
 * - reclaim space used by unreachable nodes after move promotion
 * - prune tree down to max 20% capacity
 * - prune large trees (>40k playouts) keeping only nodes with enough playouts.
 * See also LARGE_TREE_PLAYOUTS, DEEP_PLAYOUTS_THRESHOLD above.
 * Expensive, especially for huge trees, needs to copy the whole tree twice. */
void
tree_garbage_collect(tree_t *tree)
{
	tree_node_t *node = tree->root;
	assert(tree->nodes && !node->parent && !node->sibling);
	double start_time = time_now();
	size_t orig_size = tree->nodes_size;

	tree_t *temp_tree = tree_init(tree->board, tree->root_color, tree->max_pruned_size, 0, 0, 0);

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
	tree_prune(temp_tree, tree, threshold, max_depth);

	/* Now copy back to original tree. */
	tree_copy(tree, temp_tree);
	tree_node_t *new_node = tree->root;

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
}

/* Copy the whole tree (all reachable nodes)
 * dst tree must be able to hold src's content.
 * The relative order of children of a given node is preserved
 * (assumed by tree_get_node() in particular).
 * Note: Only for fast_alloc. */
void
tree_copy(tree_t *dst, tree_t *src)
{
	tree_prune(dst, src, 0, src->max_depth);
	assert(dst->root);
}


/* Realloc internal tree memory so it can accomodate bigger search tree
 * Expensive: needs to allocate a new tree and copy it over.
 * returns 1 if successful
 *         0 if failed (out of memory) */
int
tree_realloc(tree_t *t, size_t max_tree_size, size_t max_pruned_size, size_t pruning_threshold)
{
	assert(max_tree_size > t->max_tree_size);
	assert(max_pruned_size > t->max_pruned_size);
	assert(pruning_threshold > t->pruning_threshold);

	tree_t *t2 = tree_init(t->board, stone_other(t->root_color), max_tree_size, max_pruned_size,
			       pruning_threshold, tree_hbits(t));
	if (!t2)  return 0;	/* Out of memory */

	tree_copy(t2, t);	assert(t2->root_color == t->root_color);
	tree_replace(t, t2);
	return 1;
}

/* tree <- content
 * makes @tree be @content without changing tree_t pointers (cheap)
 * afterwards @tree prev content and @content pointer are invalid */
void
tree_replace(tree_t *tree, tree_t *content)
{
	tree_t *tmp = malloc2(tree_t);
	*tmp = *tree;      tree_done(tmp);
	*tree = *content;  free(content);
}


/* Find node of given coordinate under parent. */
tree_node_t *
tree_get_node(tree_node_t *parent, coord_t c)
{
	for (tree_node_t *n = parent->children; n; n = n->sibling)
		if (node_coord(n) == c)
			return n;
	return NULL;
}


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

	/* Now, create the nodes (all at once) */
	tree_node_t *ni = tree_alloc_node(t, child_count);
	/* We might temporarily run out of nodes but this should be rare. */
	if (!ni) {
		node->is_expanded = false;
		return;
	}
	tree_setup_node(t, ni, pass, node->depth + 1);

	tree_node_t *first_child = ni;
	ni->parent = node;
	ni->prior = map.prior[pass]; ni->d = TREE_NODE_D_MAX + 1;

	int child = 1;
	foreach_point(board) {
		if (!map.consider[c]) // Filter out invalid moves
			continue;
		assert(c != node_coord(node)); // I have spotted "C3 C3" in some sequence...
		
		tree_node_t *nj = first_child + child++;
		tree_setup_node(t, nj, c, node->depth + 1);
		nj->parent = node; ni->sibling = nj; ni = nj;
		
		ni->prior = map.prior[c];
		ni->d = distances[c];
	} foreach_point_end;
	node->children = first_child; // must be done at the end to avoid race
}

/* Promotes the given node as the root of the tree.
 * May trigger tree garbage collection:
 * The node may be moved and some of its subtree may be pruned. */
void
tree_promote_node(tree_t *t, tree_node_t *node)
{
	assert(node->parent == t->root);

	node->parent = NULL;
	node->sibling = NULL;

	t->root = node;
	t->root_color = stone_other(t->root_color);
	
	/* Garbage collect if we run out of memory, or it is cheap to do so now: */
	if (t->nodes_size >= t->pruning_threshold ||
	    (t->nodes_size >= t->max_tree_size / 10 && node->u.playouts < SMALL_TREE_PLAYOUTS))
		tree_garbage_collect(t);

	t->avg_score.playouts = 0;

	/* If the tree deepest node was under node, or if we called tree_garbage_collect,
	 * tree->max_depth is correct. Otherwise we could traverse the tree
         * to recompute max_depth but it's not worth it: it's just for debugging
	 * and soon the tree will grow and max_depth will become correct again. */
}

/* Promote node for given move as the root of the tree.
 * May trigger tree garbage collection:
 * The node may be moved and some of its subtree may be pruned.
 * Returns true on success, false otherwise (@reason tells why) */
bool
tree_promote_move(tree_t *t, board_t *b, move_t *m, int *reason)
{
	*reason = 0;

	if (m->color != stone_other(t->root_color))
		return false;  /* Bad color */
	
	tree_node_t *n = tree_get_node(t->root, m->coord);
	if (!n)  return false;
	
	if (using_dcnn(b) && !(n->hints & TREE_HINT_DCNN)) {
		*reason = TREE_HINT_DCNN;
		return false;  /* No dcnn priors, can't reuse ... */
	}
	
	tree_promote_node(t, n);
	return true;
}
