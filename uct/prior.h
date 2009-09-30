#ifndef ZZGO_UCT_PRIOR_H
#define ZZGO_UCT_PRIOR_H

#include "move.h"

struct tree;
struct tree_node;
struct uct;
struct board;

void uct_prior(struct uct *u, struct tree *tree, struct tree_node *node,
               struct board *b, enum stone color, int parity);

#endif
