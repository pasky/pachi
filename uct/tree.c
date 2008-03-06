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


static struct tree_node *
tree_init_node(coord_t coord)
{
	struct tree_node *n = calloc(1, sizeof(*n));
	n->coord = coord;
	return n;
}

struct tree *
tree_init(struct board *board)
{
	struct tree *t = calloc(1, sizeof(*t));
	/* The root PASS move is only virtual, we never play it. */
	t->root = tree_init_node(pass);
	t->board = board;
	return t;
}


static void
tree_done_node(struct tree_node *n)
{
	struct tree_node *ni = n->children;
	while (ni) {
		struct tree_node *nj = ni->sibling;
		tree_done_node(ni);
		ni = nj;
	}
	free(n);
}

void
tree_done(struct tree *t)
{
	tree_done_node(t->root);
	free(t);
}


static void
tree_node_dump(struct tree *tree, struct tree_node *node, int l)
{
	for (int i = 0; i < l; i++) fputc(' ', stderr);
	fprintf(stderr, "[%s] %f (%d playouts)\n", coord2sstr(node->coord, tree->board), node->value, node->playouts);

	/* Print nodes sorted by #playouts. */

	struct tree_node *nbox[1000]; int nboxl = 0;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		if (ni->playouts > 200)
			nbox[nboxl++] = ni;

	while (true) {
		int best = -1;
		for (int i = 0; i < nboxl; i++)
			if (nbox[i] && (best < 0 || nbox[i]->playouts > nbox[best]->playouts))
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
tree_expand_node(struct tree *t, struct tree_node *node, struct board *b)
{
	assert(!node->children);

	struct tree_node *ni = tree_init_node(pass);
	ni->parent = node; node->children = ni;

	/* The loop excludes the offboard margin. */
	for (int i = 1; i < t->board->size; i++) {
		for (int j = 1; j < t->board->size; j++) {
			coord_t c = coord_xy_otf(i, j, t->board);
			if (board_at(b, c) != S_NONE)
				continue;
			struct tree_node *nj = tree_init_node(coord_xy_otf(i, j, t->board));
			nj->parent = node; ni->sibling = nj; ni = nj;
		}
	}
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
}

void
tree_delete_node(struct tree_node *node)
{
	tree_unlink_node(node);
	assert(!node->children);
	free(node);
}

void
tree_promote_node(struct tree *tree, struct tree_node *node)
{
	assert(node->parent == tree->root);
	tree_unlink_node(node);
	tree_done_node(tree->root);
	tree->root = node;
	node->parent = NULL;
}

bool
tree_leaf_node(struct tree_node *node)
{
	return !(node->children);
}

struct tree_node *
tree_best_child(struct tree_node *node)
{
	struct tree_node *nbest = node->children;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		// we compare playouts and choose the best-explored
		// child; comparing values is more brittle
		if (ni->playouts > nbest->playouts)
			nbest = ni;
	return nbest;
}


struct tree_node *
tree_uct_descend(struct tree *tree, struct tree_node *node, int parity)
{
	float xpl = log(node->playouts) * tree->explore_p;

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
#ifdef UCB1_TUNED
		float xpl_loc = (ni->value - ni->value * ni->value);
		if (parity < 0) xpl_loc = 1 - xpl_loc;
		xpl_loc += sqrt(xpl / ni->playouts);
		if (xpl_loc > 1.0/4) xpl_loc = 1.0/4;
		float urgency = ni->value * parity + sqrt(xpl * xpl_loc / ni->playouts);
#else
		float urgency = ni->value * parity + sqrt(xpl / ni->playouts);
#endif
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

void
tree_uct_update(struct tree_node *node, int result)
{
	for (; node; node = node->parent) {
		node->playouts++;
		node->wins += result;
		node->value = (float)node->wins / node->playouts;
	}
}
