#ifndef ZZGO_UCT_POLICY_GENERIC_H
#define ZZGO_UCT_POLICY_GENERIC_H

/* Some default policy routines and templates. */

#include "stone.h"
#include "uct/internal.h"

struct board;
struct tree_node;

struct tree_node *uctp_generic_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

#endif
