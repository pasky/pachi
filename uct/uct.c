#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "move.h"
#include "playout.h"
#include "playout/moggy.h"
#include "playout/old.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/uct.h"

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1tuned_init(struct uct *u, char *arg);


#define MC_GAMES	40000
#define MC_GAMELEN	400


static coord_t
domainhint_policy(void *playout_policy, struct board *b, enum stone my_color)
{
	struct uct *u = playout_policy;
	return u->playout(&u->mc, b, my_color);
}

static int
uct_playout(struct uct *u, struct board *b, enum stone color, struct tree *t)
{
	struct board b2;
	board_copy(&b2, b);

	/* Walk the tree until we find a leaf, then expand it and do
	 * a random playout. */
	struct tree_node *n = t->root;
	enum stone orig_color = color;
	int result;
	int pass_limit = (b2.size - 2) * (b2.size - 2) / 2;
	int passes = is_pass(b->last_move.coord);
	if (UDEBUGL(8))
		fprintf(stderr, "--- UCT walk with color %d\n", color);
	for (; pass; color = stone_other(color)) {
		if (tree_leaf_node(n)) {
			if (n->pos->playouts >= u->expand_p)
				tree_expand_node(t, n, &b2, color);

			result = play_random_game(&b2, color, u->gamelen, domainhint_policy, u);
			if (orig_color != color && result >= 0)
				result = !result;
			if (UDEBUGL(7))
				fprintf(stderr, "[%d..%d] %s random playout result %d\n", orig_color, color, coord2sstr(n->coord, t->board), result);
			break;
		}

		n = u->policy->descend(u->policy, t, n, (color == orig_color ? 1 : -1), pass_limit);
		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "-- UCT sent us to [%s] %f\n", coord2sstr(n->coord, t->board), n->pos->value);
		struct move m = { n->coord, color };
		int res = board_play(&b2, &m);
		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */
		    || b2.superko_violation) {
			if (UDEBUGL(6))
				fprintf(stderr, "deleting invalid node %d,%d\n", coord_x(n->coord,b), coord_y(n->coord,b));
			tree_delete_node(t, n);
			board_done_noalloc(&b2);
			return -1;
		}

		if (is_pass(n->coord)) {
			passes++;
			if (passes >= 2) {
				float score = board_official_score(&b2);
				result = (orig_color == S_BLACK) ? score < 0 : score > 0;
				if (UDEBUGL(5))
					fprintf(stderr, "[%d..%d] %s playout result %d (W %f)\n", orig_color, color, coord2sstr(n->coord, t->board), result, score);
				if (UDEBUGL(6))
					board_print(&b2, stderr);
				break;
			}
		} else {
			passes = 0;
		}
	}

	assert(n == t->root || n->parent);
	if (result >= 0)
		tree_uct_update(n, result);
	board_done_noalloc(&b2);
	return result;
}

static coord_t *
uct_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;

	if (!u->t) {
tree_init:
		u->t = tree_init(b, color);
		//board_print(b, stderr);
	} else {
		/* XXX: We hope that the opponent didn't suddenly play
		 * several moves in the row. */
		for (struct tree_node *ni = u->t->root->pos->children; ni; ni = ni->sibling)
			if (ni->coord == b->last_move.coord) {
				tree_promote_node(u->t, ni);
				goto promoted;
			}
		fprintf(stderr, "CANNOT FIND NODE TO PROMOTE!\n");
		tree_done(u->t);
		goto tree_init;
promoted:;
	}

	int i;
	for (i = 0; i < u->games; i++) {
		int result = uct_playout(u, b, color, u->t);
		if (result < 0) {
			/* Tree descent has hit invalid move. */
			continue;
		}

		if (i > 0 && !(i % 1000)) {
			struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
			if (best && best->pos->playouts >= 500 && best->pos->value >= u->loss_threshold)
				break;
		}
	}

	if (UDEBUGL(2))
		tree_dump(u->t);

	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
	if (!best) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(pass);
	}
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %d,%d with score %1.4f (%d games, %d/%d positions reused)\n", coord_x(best->coord, b), coord_y(best->coord, b), best->pos->value, i, u->t->reused_pos, u->t->total_pos);
	if (best->pos->value < u->resign_ratio && !is_pass(best->coord)) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(resign);
	}
	tree_promote_node(u->t, best);
	return coord_copy(best->coord);
}


struct uct *
uct_state_init(char *arg)
{
	struct uct *u = calloc(1, sizeof(struct uct));

	u->debug_level = 1;
	u->games = MC_GAMES;
	u->gamelen = MC_GAMELEN;
	u->expand_p = 2;
	u->mc.capture_rate = 100;
	u->mc.atari_rate = 100;
	u->mc.cut_rate = 0;
	// Looking at the actual playouts, this just encourages MC to make
	// stupid shapes.
	u->mc.local_rate = 0;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					u->debug_level = atoi(optval);
				else
					u->debug_level++;
			} else if (!strcasecmp(optname, "games") && optval) {
				u->games = atoi(optval);
			} else if (!strcasecmp(optname, "gamelen") && optval) {
				u->gamelen = atoi(optval);
			} else if (!strcasecmp(optname, "expand_p") && optval) {
				u->expand_p = atoi(optval);
			} else if (!strcasecmp(optname, "policy") && optval) {
				char *policyarg = strchr(optval, ':');
				if (policyarg)
					*policyarg++ = 0;
				if (!strcasecmp(optval, "ucb1")) {
					u->policy = policy_ucb1_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1tuned")) {
					u->policy = policy_ucb1tuned_init(u, policyarg);
				}
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "old")) {
					u->playout = playout_old;
				} else if (!strcasecmp(optval, "moggy")) {
					u->playout = playout_moggy;
				}
			} else if (!strcasecmp(optname, "pure")) {
				u->mc.capture_rate = u->mc.local_rate = u->mc.cut_rate = 0;
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				u->mc.capture_rate = atoi(optval);
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				u->mc.atari_rate = atoi(optval);
			} else if (!strcasecmp(optname, "localrate") && optval) {
				u->mc.local_rate = atoi(optval);
			} else if (!strcasecmp(optname, "cutrate") && optval) {
				u->mc.cut_rate = atoi(optval);
			} else {
				fprintf(stderr, "uct: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	u->resign_ratio = 0.2; /* Resign when most games are lost. */
	u->loss_threshold = 0.95; /* Stop reading if after at least 500 playouts this is best value. */
	u->mc.debug_level = u->debug_level;
	if (!u->policy)
		u->policy = policy_ucb1_init(u, NULL);
	if (!u->playout)
		u->playout = playout_old;

	return u;
}


struct engine *
engine_uct_init(char *arg)
{
	struct uct *u = uct_state_init(arg);
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "UCT Engine";
	e->comment = "I'm playing UCT. When we both pass, I will consider all the stones on the board alive. If you are reading this, write 'yes'. Please bear with me at the game end, I need to fill the whole board; if you help me, we will both be happier. Filling the board will not lose points (NZ rules).";
	e->genmove = uct_genmove;
	e->data = u;

	return e;
}
