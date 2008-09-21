#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"
#include "uct/internal.h"
#include "uct/tree.h"


static struct tree_node *
tree_init_node(struct tree *t, coord_t coord, int depth)
{
	struct tree_node *n = calloc(1, sizeof(*n));
	n->coord = coord;
	n->depth = depth;
	if (depth > t->max_depth)
		t->max_depth = depth;
	return n;
}

struct tree *
tree_init(struct board *board, enum stone color)
{
	struct tree *t = calloc(1, sizeof(*t));
	t->board = board;
	/* The root PASS move is only virtual, we never play it. */
	t->root = tree_init_node(t, pass, 0);
	return t;
}


static void
tree_done_node(struct tree *t, struct tree_node *n)
{
	struct tree_node *ni = n->children;
	while (ni) {
		struct tree_node *nj = ni->sibling;
		tree_done_node(t, ni);
		ni = nj;
	}
	free(n);
}

void
tree_done(struct tree *t)
{
	tree_done_node(t, t->root);
	free(t);
}


static void
tree_node_dump(struct tree *tree, struct tree_node *node, int l, int thres)
{
	for (int i = 0; i < l; i++) fputc(' ', stderr);
	fprintf(stderr, "[%s] %f (%d/%d playouts [prior %d/%d amaf %d/%d]; hints %x)\n", coord2sstr(node->coord, tree->board), node->u.value, node->u.wins, node->u.playouts, node->prior.wins, node->prior.playouts, node->amaf.wins, node->amaf.playouts, node->hints);

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
		tree_node_dump(tree, nbox[best], l + 1, thres);
		nbox[best] = NULL;
	}
}

void
tree_dump(struct tree *tree, int thres)
{
	tree_node_dump(tree, tree->root, 0, thres);
}


static char *
tree_book_name(struct board *b)
{
	static char buf[256];
	sprintf(buf, "uct-%d-%02.01f.pachibook", b->size - 2, b->komi);
	return buf;
}

static void
tree_node_save(FILE *f, struct tree_node *node, int thres)
{
	if (node->u.playouts < thres)
		return;

	fputc(1, f);
	fwrite(((void *) node) + offsetof(struct tree_node, depth),
	       sizeof(struct tree_node) - offsetof(struct tree_node, depth),
	       1, f);

	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		tree_node_save(f, ni, thres);
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

	fread(((void *) node) + offsetof(struct tree_node, depth),
	       sizeof(struct tree_node) - offsetof(struct tree_node, depth),
	       1, f);

	struct tree_node *ni = NULL, *ni_prev = NULL;
	while (fgetc(f)) {
		ni_prev = ni; ni = calloc(1, sizeof(*ni));
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

	fprintf(stderr, "Loading opening book %s...\n", filename);

	int num = 0;
	if (fgetc(f))
		tree_node_load(f, tree->root, &num);
	fprintf(stderr, "Loaded %d nodes.\n", num);

	fclose(f);
}


/* Tree symmetry: When possible, we will localize the tree to a single part
 * of the board in tree_expand_node() and possibly flip along symmetry axes
 * to another part of the board in tree_promote_at(). We follow b->symmetry
 * guidelines here. */


void
tree_expand_node(struct tree *t, struct tree_node *node, struct board *b, enum stone color, int radar, struct uct_policy *policy, int parity)
{
	struct tree_node *ni = tree_init_node(t, pass, node->depth + 1);
	ni->parent = node; node->children = ni;

	/* The loop considers only the symmetry playground. */
	for (int i = b->symmetry.x1; i <= b->symmetry.x2; i++) {
		for (int j = b->symmetry.y1; j <= b->symmetry.y2; j++) {
			if (b->symmetry.d) {
				int x = b->symmetry.type == SYM_DIAG_DOWN ? board_size(b) - i : i;
				if (b->symmetry.d < 0 ? x < j : x > j)
					continue;
			}

			coord_t c = coord_xy_otf(i, j, t->board);
			if (board_at(b, c) != S_NONE)
				continue;
			/* This looks very useful on large boards - weeds out huge amount of crufty moves. */
			if (b->hash /* not empty board */ && radar && !board_stone_radar(b, c, radar))
				continue;

			struct tree_node *nj = tree_init_node(t, c, node->depth + 1);
			nj->parent = node; ni->sibling = nj; ni = nj;

			if (policy->prior)
				policy->prior(policy, t, ni, b, color, parity);
		}
	}
}


static void
tree_fix_node_symmetry(struct board *b, struct tree_node *node,
                       bool flip_horiz, bool flip_vert, int flip_diag)
{
	int x = coord_x(node->coord, b), y = coord_y(node->coord, b);
	if (flip_diag) {
		int z = x;
		x = flip_diag == 1 ? y : board_size(b) - y;
		y = flip_diag == 1 ? z : board_size(b) - z;
	}
	if (flip_horiz) {
		x = board_size(b) - x;
	}
	if (flip_vert) {
		y = board_size(b) - y;
	}
	node->coord = coord_xy_otf(x, y, b);

	for (struct tree_node *ni = node->children; ni; ni = ni->sibling)
		tree_fix_node_symmetry(b, ni, flip_horiz, flip_vert, flip_diag);
}

static void
tree_fix_symmetry(struct tree *tree, struct board *b, coord_t c)
{
	/* XXX: We hard-coded assume c is the same move that tree-root,
	 * just possibly flipped. */

	if (c == tree->root->coord)
		return;

	int cx = coord_x(c, b), cy = coord_y(c, b);
	int rx = coord_x(tree->root->coord, b), ry = coord_y(tree->root->coord, b);

	/* playground	X->h->v->d normalization
	 * :::..	.d...
	 * .::..	v....
	 * ..:..	.....
	 * .....	h...X
	 * .....	.....  */
	bool flip_horiz = cy == ry;
	bool flip_vert = cx == rx;

	int nx = flip_horiz ? board_size(b) - rx : rx;
	int ny = flip_vert ? board_size(b) - ry : ry;

	int flip_diag = 0;
	if (nx == cy && ny == cx) {
		flip_diag = 1;
	} else if (board_size(b) - nx == cy && ny == board_size(b) - cx) {
		flip_diag = 2;
	}

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
tree_promote_at(struct tree *tree, struct board *b, coord_t c)
{
	tree_fix_symmetry(tree, b, c);

	for (struct tree_node *ni = tree->root->children; ni; ni = ni->sibling)
		if (ni->coord == c) {
			tree_promote_node(tree, ni);
			return true;
		}
	return false;
}

bool
tree_leaf_node(struct tree_node *node)
{
	return !(node->children);
}

void
tree_update_node_value(struct tree_node *node, bool add_amaf)
{
	node->u.value = (float)(node->u.wins + node->prior.wins + (add_amaf ? node->amaf.wins : 0))
			/ (node->u.playouts + node->prior.playouts + (add_amaf ? node->amaf.playouts : 0));
#if 0
	{ struct board b2; board_size(&b2) = 9+2;
	fprintf(stderr, "%s->%s %d/%d %d/%d %f\n", node->parent ? coord2sstr(node->parent->coord, &b2) : NULL, coord2sstr(node->coord, &b2), node->u.wins, node->u.playouts, node->prior.wins, node->prior.playouts, node->u.value); }
#endif
}
