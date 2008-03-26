#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"
#include "uct/tree.h"


static struct boardpos *
init_boardpos(struct tree *t, hash_t hash)
{
	int h;
	for (h = hash & uct_hash_mask; t->positions[h]; h = uct_hash_next(h))
		if (t->positions[h]->hash == hash)
			return t->positions[h];

	struct boardpos *pos = calloc(1, sizeof(*pos));
	pos->hash = hash;
	t->positions[h] = pos;
	return pos;
}

static struct tree_node *
tree_init_node(struct tree *t, coord_t coord, struct boardpos *pos)
{
	struct tree_node *n = calloc(1, sizeof(*n));
	n->coord = coord;
	n->hash = pos->hash;
	n->pos = pos;
	pos->refcount++;
	if (pos->refcount > 1)
		t->reused_pos++;
	t->total_pos++;
#if 0
	for (int h = n->hash & uct_hash_mask; 1; h = uct_hash_next(h))
		if (!t->nodes[h]) {
			t->nodes[h] = n;
			break;
		}
#endif
	return n;
}

struct tree *
tree_init(struct board *board, enum stone color)
{
	struct tree *t = calloc(1, sizeof(*t));
	/* The root PASS move is only virtual, we never play it. */
	t->root = tree_init_node(t, pass, init_boardpos(t, 0 ^ color));
	t->board = board;
	return t;
}


static void tree_done_node(struct tree *t, struct tree_node *n);

static void
remove_hash_from_chain(struct tree *t, struct boardpos *pos, hash_t h)
{
	/* Now find a hash further in line to put in here.  This is not as
	 * simple, because hashes after this one might not be reachable from
	 * current bucket.  We also need to consider wraparound to zero when
	 * comparing hashes. (Please note that h got hash_t now!) */
	/* Even so we do not account for hash values near ~(0ULL).
	 * Too rare. */
	hash_t h2 = h, h_nice = h;
	for (h2++; t->positions[h2 & uct_hash_mask]; h2++) {
		struct boardpos *p2 = t->positions[h2 & uct_hash_mask];
		assert(p2->hash != pos->hash);
		assert((h2 & uct_hash_mask) >= (p2->hash & uct_hash_mask));
		if ((p2->hash & uct_hash_mask) <= (h & uct_hash_mask)) {
			/* We can put this one at our position. */
			h_nice = h2;
		}
	}
	if (h_nice == h) {
		/* We can safely break the chain. */
		t->positions[h & uct_hash_mask] = NULL;
		return;
	}
	/* Put the most distantly placed hash in our place and repair the rest
	 * of the chain recursively. */
	t->positions[h] = t->positions[h_nice];
	remove_hash_from_chain(t, t->positions[h], h_nice);
}

static void
done_boardpos(struct tree *t, struct boardpos *pos, struct tree_node *n)
{
	if (--pos->refcount > 0) {
		/* Sanity check... */
		struct tree_node *ni = pos->children;
		bool had_children = (ni != NULL);
		bool removed_child = false;
		while (ni) {
			struct tree_node *nj = ni->sibling;
			if (ni->parent == n) {
				/* This does not happen frequently enough
				 * to warrant a hash of tree nodes, I think.
				 * TODO: Measure ELO impact. */
				if (ni->pos->playouts >= 200)
					/* This is slightly anomalous but
					 * can happen in imminent superko
					 * situation sometimes. */
					fprintf(stderr, "Orphaned a node with %d playouts!\n", ni->pos->playouts);
				tree_delete_node(t, ni);
				removed_child = true;
			}
			ni = nj;
		}
		/* If we removed one child, we should've removed them all
		 * since we add all childs at once. Thus, we will simply
		 * re-expand the node the next time we visit it. */
		assert(!(had_children && removed_child && pos->children));
		/* ...and bye. */
		return;
	}

	struct tree_node *ni = pos->children;
	while (ni) {
		struct tree_node *nj = ni->sibling;
		assert(ni->parent->hash == pos->hash); // Another sanity check
		tree_done_node(t, ni);
		ni = nj;
	}
	bool found = false;
	for (int h = pos->hash & uct_hash_mask; t->positions[h]; h = uct_hash_next(h))
		if (t->positions[h] == pos) {
			assert(h >= (t->positions[h]->hash & uct_hash_mask));
			remove_hash_from_chain(t, pos, h);
			found = true;
			break;
		}
	assert(found);
	free(pos);
}

static void
tree_done_node(struct tree *t, struct tree_node *n)
{
	if (n->pos->refcount > 1)
		t->reused_pos--;
	t->total_pos--;
	done_boardpos(t, n->pos, n);
#if 0
	for (int h = n->hash & uct_hash_mask; !t->nodes[h]; h = uct_hash_next(h))
		if (t->nodes[h] == pos) {
			int h2 = uct_hash_next(h);
			while (t->nodes[h2])
				h2 = uct_hash_next(h);
			t->nodes[h] = (h2 == uct_hash_next(h) ? NULL : uct_hash_prev(h2));
		}
#endif
	free(n);
}

void
tree_done(struct tree *t)
{
	tree_done_node(t, t->root);
	free(t);
}


static void
tree_node_dump(struct tree *tree, struct tree_node *node, int l)
{
	for (int i = 0; i < l; i++) fputc(' ', stderr);
	fprintf(stderr, "[%s] %f (%d/%d playouts)\n", coord2sstr(node->coord, tree->board), node->pos->value, node->pos->wins, node->pos->playouts);

	/* Print nodes sorted by #playouts. */

	struct tree_node *nbox[1000]; int nboxl = 0;
	for (struct tree_node *ni = node->pos->children; ni; ni = ni->sibling)
		if (ni->pos->playouts > 200)
			nbox[nboxl++] = ni;

	while (true) {
		int best = -1;
		for (int i = 0; i < nboxl; i++)
			if (nbox[i] && (best < 0 || nbox[i]->pos->playouts > nbox[best]->pos->playouts))
				best = i;
		if (best < 0)
			break;
		tree_node_dump(tree, nbox[best], l + 1);
		nbox[best] = NULL;
	}
}

void
tree_dump(struct tree *tree)
{
	tree_node_dump(tree, tree->root, 0);
}


void
tree_expand_node(struct tree *t, struct tree_node *node, struct board *b, enum stone color)
{
	assert(!node->pos->children);

	struct tree_node *ni = tree_init_node(t, pass, init_boardpos(t, node->hash ^ color));
	ni->parent = node; node->pos->children = ni;

	/* The loop excludes the offboard margin. */
	for (int i = 1; i < t->board->size; i++) {
		for (int j = 1; j < t->board->size; j++) {
			coord_t c = coord_xy_otf(i, j, t->board);
			if (board_at(b, c) != S_NONE)
				continue;
			struct tree_node *nj = tree_init_node(t, c, init_boardpos(t, node->hash ^ hash_at(b, c, color)));
			nj->parent = node; ni->sibling = nj; ni = nj;
		}
	}
}

static void
tree_unlink_node(struct tree_node *node)
{
	struct tree_node *ni = node->parent;
	if (ni->pos->children == node) {
		ni->pos->children = node->sibling;
	} else {
		ni = ni->pos->children;
		while (ni->sibling != node)
			ni = ni->sibling;
		ni->sibling = node->sibling;
	}
}

void
tree_delete_node(struct tree *tree, struct tree_node *node)
{
	tree_unlink_node(node);
	tree_done_node(tree, node);
}

void
tree_promote_node(struct tree *tree, struct tree_node *node)
{
	assert(node->parent == tree->root);
	tree_unlink_node(node);
	tree_done_node(tree, tree->root);
	tree->root = node;
	node->parent = NULL;
}

bool
tree_leaf_node(struct tree_node *node)
{
	return !(node->pos->children);
}

void
tree_uct_update(struct tree_node *node, int result)
{
	/* It is enough to iterate by a single chain; we will
	 * update all the preceding positions properly since
	 * they had to all occur in all branches, only in
	 * different order. */
	for (; node; node = node->parent) {
		struct boardpos *pos = node->pos;
		pos->playouts++;
		pos->wins += result;
		pos->value = (float)pos->wins / pos->playouts;
	}
}
