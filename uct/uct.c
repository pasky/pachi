#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "move.h"
#include "playout.h"
#include "montecarlo/hint.h"
#include "montecarlo/internal.h"
#include "uct/tree.h"
#include "uct/uct.h"



#define MC_GAMES	40000
#define MC_GAMELEN	400


/* Internal engine state. */
struct uct {
	int debug_level;
	int games, gamelen;
	float resign_ratio;
	float loss_threshold;
	float explore_p;
	int expand_p;

	struct montecarlo mc;
	struct tree *t;
};

#define UDEBUGL(n) DEBUGL_(u->debug_level, n)


static coord_t
domainhint_policy(void *playout_policy, struct board *b, enum stone my_color)
{
	struct uct *u = playout_policy;
	return domain_hint(&u->mc, b, my_color);
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
	int passes = 0;
	if (UDEBUGL(8))
		fprintf(stderr, "--- UCT walk\n");
	for (; pass; color = stone_other(color)) {
		if (tree_leaf_node(n)) {
			if (n->playouts >= u->expand_p)
				tree_expand_node(t, n, &b2);

			struct move m = { n->coord, color };
			result = play_random_game(&b2, &m, u->gamelen, domainhint_policy, u);
			if (orig_color != color && result >= 0)
				result = !result;
			if (UDEBUGL(7))
				fprintf(stderr, "[%d..%d] %s playout result %d\n", orig_color, color, coord2sstr(n->coord, t->board), result);
			break;
		}

		n = tree_uct_descend(t, n, (color == orig_color ? 1 : -1), b2.moves > (b2.size2 - 2) / 2);
		if (UDEBUGL(7))
			fprintf(stderr, "-- UCT sent us to [%s] %f\n", coord2sstr(n->coord, t->board), n->value);
		struct move m = { n->coord, color };
		int res = board_play(&b2, &m);
		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */) {
			if (UDEBUGL(6))
				fprintf(stderr, "deleting invalid node %d,%d\n", coord_x(n->coord,b), coord_y(n->coord,b));
			tree_delete_node(n);
			board_done_noalloc(&b2);
			return -1;
		}

		if (is_pass(n->coord)) {
			passes++;
			if (passes >= 2) {
				float score = board_fast_score(&b2) > 0;
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
		u->t = tree_init(b);
		u->t->explore_p = u->explore_p;
	} else {
		/* XXX: We hope that the opponent didn't suddenly play
		 * several moves in the row. */
		for (struct tree_node *ni = u->t->root->children; ni; ni = ni->sibling)
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
			struct tree_node *best = tree_best_child(u->t->root);
			if (best && best->playouts >= 100 && best->value >= u->loss_threshold)
				break;
		}
	}

	if (UDEBUGL(2))
		tree_dump(u->t);

	struct tree_node *best = tree_best_child(u->t->root);
	if (!best) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(pass);
	}
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %d,%d with score %1.4f (%d games)\n", coord_x(best->coord, b), coord_y(best->coord, b), best->value, i);
	if (best->value < u->resign_ratio && !is_pass(best->coord)) {
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
	u->explore_p = 0.2;
	u->expand_p = 2;
	u->mc.capture_rate = 100;
	u->mc.atari_rate = 100;
	u->mc.cut_rate = 50;
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
			} else if (!strcasecmp(optname, "explore_p") && optval) {
				u->explore_p = atof(optval);
			} else if (!strcasecmp(optname, "expand_p") && optval) {
				u->expand_p = atoi(optval);
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
	u->loss_threshold = 0.9; /* Stop reading if after at least 1000 games this is best value. */
	u->mc.debug_level = u->debug_level;

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
