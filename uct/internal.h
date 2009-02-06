#ifndef ZZGO_UCT_INTERNAL_H
#define ZZGO_UCT_INTERNAL_H

#include "debug.h"
#include "move.h"
#include "playout.h"

struct tree;
struct tree_node;
struct uct_policy;

/* Internal UCT structures */


/* Internal engine state. */
struct uct {
	int debug_level;
	int games, gamelen;
	float resign_ratio;
	float loss_threshold;
	int expand_p;
	int radar_d;
	bool playout_amaf;
	int dumpthres;
	int threads;

	struct uct_policy *policy;
	struct tree *t;
	struct playout_policy *playout;
};

#define UDEBUGL(n) DEBUGL_(u->debug_level, n)


typedef struct tree_node *(*uctp_choose)(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);
typedef struct tree_node *(*uctp_descend)(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);
typedef void (*uctp_prior)(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int parity);
typedef void (*uctp_update)(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone color, struct playout_amafmap *amaf, int result);

struct uct_policy {
	struct uct *uct;
	uctp_choose choose;
	uctp_descend descend;
	uctp_update update;
	uctp_prior prior;
	bool wants_amaf;
	void *data;
};

#endif
