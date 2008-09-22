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
#include "playout/light.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/uct.h"

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1tuned_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg);


#define MC_GAMES	40000
#define MC_GAMELEN	400


static void
progress_status(struct uct *u, struct tree *t, enum stone color, int playouts)
{
	if (!UDEBUGL(0))
		return;

	/* Best move */
	struct tree_node *best = u->policy->choose(u->policy, t->root, t->board, color);
	if (!best) {
		fprintf(stderr, "... No moves left\n");
		return;
	}
	fprintf(stderr, "[%d] ", playouts);
	fprintf(stderr, "best %f ", best->u.value);

	/* Max depth */
	fprintf(stderr, "deepest % 2d ", t->max_depth - t->root->depth);

	/* Best sequence */
	fprintf(stderr, "| seq ");
	for (int depth = 0; depth < 6; depth++) {
		if (best && best->u.playouts >= 25) {
			fprintf(stderr, "%3s ", coord2sstr(best->coord, t->board));
			best = u->policy->choose(u->policy, best, t->board, color);
		} else {
			fprintf(stderr, "    ");
		}
	}

	/* Best candidates */
	fprintf(stderr, "| can ");
	int cans = 4;
	struct tree_node *can[cans];
	memset(can, 0, sizeof(can));
	best = t->root->children;
	while (best) {
		int c = 0;
		while ((!can[c] || best->u.playouts > can[c]->u.playouts) && ++c < cans);
		for (int d = 0; d < c; d++) can[d] = can[d + 1];
		if (c > 0) can[c - 1] = best;
		best = best->sibling;
	}
	while (--cans >= 0) {
		if (can[cans]) {
			fprintf(stderr, "%3s(%.3f) ", coord2sstr(can[cans]->coord, t->board), can[cans]->u.value);
		} else {
			fprintf(stderr, "           ");
		}
	}

	fprintf(stderr, "\n");
}


static int
uct_playout(struct uct *u, struct board *b, enum stone color, struct tree *t)
{
	struct board b2;
	board_copy(&b2, b);

	struct playout_amafmap *amaf = NULL;
	if (u->policy->wants_amaf) {
		amaf = calloc(1, sizeof(*amaf));
		amaf->map = calloc(board_size2(&b2) + 1, sizeof(*amaf->map));
		amaf->map++; // -1 is pass
	}

	/* Walk the tree until we find a leaf, then expand it and do
	 * a random playout. */
	struct tree_node *n = t->root;
	enum stone orig_color = color;
	int result;
	int pass_limit = (board_size(&b2) - 2) * (board_size(&b2) - 2) / 2;
	int passes = is_pass(b->last_move.coord);
	if (UDEBUGL(8))
		fprintf(stderr, "--- UCT walk with color %d\n", color);
	for (; pass; color = stone_other(color)) {
		if (tree_leaf_node(n)) {
			if (n->u.playouts >= u->expand_p)
				tree_expand_node(t, n, &b2, color, u->radar_d, u->policy, (color == orig_color ? 1 : -1));

			result = play_random_game(&b2, color, u->gamelen, u->playout_amaf ? amaf : NULL, u->playout);
			if (orig_color != color && result >= 0)
				result = !result;
			if (UDEBUGL(7))
				fprintf(stderr, "[%d..%d] %s random playout result %d\n", orig_color, color, coord2sstr(n->coord, t->board), result);

			/* Reset color to the @n color. */
			color = stone_other(color);
			break;
		}

		n = u->policy->descend(u->policy, t, n, (color == orig_color ? 1 : -1), pass_limit);
		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "-- UCT sent us to [%s] %f\n", coord2sstr(n->coord, t->board), n->u.value);
		if (amaf && n->coord >= -1)
			amaf->map[n->coord] = color;
		struct move m = { n->coord, color };
		int res = board_play(&b2, &m);

		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */
		    || b2.superko_violation) {
			if (UDEBUGL(3)) {
				for (struct tree_node *ni = n; ni; ni = ni->parent)
					fprintf(stderr, "%s ", coord2sstr(ni->coord, t->board));
				fprintf(stderr, "deleting invalid %s node %d,%d res %d group %d spk %d\n",
				        stone2str(color), coord_x(n->coord,b), coord_y(n->coord,b),
					res, group_at(&b2, m.coord), b2.superko_violation);
			}
			tree_delete_node(t, n);
			result = -1;
			goto end;
		}

		if (is_pass(n->coord)) {
			passes++;
			if (passes >= 2) {
				float score = board_official_score(&b2);
				result = (orig_color == S_BLACK) ? score < 0 : score > 0;
				//if (UDEBUGL(5))
					fprintf(stderr, "[%d..%d] %s p-p scoring playout result %d (W %f)\n", orig_color, color, coord2sstr(n->coord, t->board), result, score);
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
		u->policy->update(u->policy, t, n, color, amaf, result);

end:
	if (amaf) {
		free(amaf->map - 1);
		free(amaf);
	}
	board_done_noalloc(&b2);
	return result;
}

static void
expand_callback(struct tree *t, struct tree_node *n, struct board *b,
                enum stone color, int parity, void *data)
{
	struct uct *u = data;
	tree_expand_node(t, n, b, color, u->radar_d, u->policy, parity);
}

static void
prepare_move(struct engine *e, struct board *b, enum stone color, coord_t promote)
{
	struct uct *u = e->data;

	if (!b->moves && u->t) {
		/* Stale state from last game */
		tree_done(u->t);
		u->t = NULL;
	}

	if (!u->t) {
		u->t = tree_init(b, color);
		//board_print(b, stderr);
		tree_load(u->t, b, color, expand_callback, u);
	}

	/* XXX: We hope that the opponent didn't suddenly play
	 * several moves in the row. */
	if (!is_resign(promote) && !tree_promote_at(u->t, b, promote)) {
		fprintf(stderr, "CANNOT FIND NODE TO PROMOTE!\n");
		/* Reset tree */
		tree_done(u->t);
		u->t = tree_init(b, color);
	}
}

static void
uct_notify_play(struct engine *e, struct board *b, struct move *m)
{
	prepare_move(e, b, stone_other(m->color), m->coord);
}

static coord_t *
uct_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;

	/* Seed the tree. */
	prepare_move(e, b, color, resign);

	int i, games = u->games - (u->t->root->u.playouts / 1.5);
	for (i = 0; i < games; i++) {
		int result = uct_playout(u, b, color, u->t);
		if (result < 0) {
			/* Tree descent has hit invalid move. */
			continue;
		}

		if (i > 0 && !(i % 10000)) {
			progress_status(u, u->t, color, i);
		}

		if (i > 0 && !(i % 500)) {
			struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
			if (best && best->u.playouts >= 1000 && best->u.value >= u->loss_threshold)
				break;
		}
	}

	progress_status(u, u->t, color, i);
	if (UDEBUGL(2))
		tree_dump(u->t, u->dumpthres);

	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
	if (!best) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(pass);
	}
	if (UDEBUGL(0))
		fprintf(stderr, "*** WINNER is %s (%d,%d) with score %1.4f (%d/%d:%d games)\n", coord2sstr(best->coord, b), coord_x(best->coord, b), coord_y(best->coord, b), best->u.value, best->u.playouts, u->t->root->u.playouts, i);
	if (best->u.value < u->resign_ratio && !is_pass(best->coord)) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(resign);
	}
	tree_promote_node(u->t, best);
	return coord_copy(best->coord);
}

bool
uct_genbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	u->t = tree_init(b, color);
	tree_load(u->t, b, color, expand_callback, u);

	int i;
	for (i = 0; i < u->games; i++) {
		int result = uct_playout(u, b, color, u->t);
		if (result < 0) {
			/* Tree descent has hit invalid move. */
			continue;
		}

		if (i > 0 && !(i % 10000)) {
			progress_status(u, u->t, color, i);
		}
	}
	progress_status(u, u->t, color, i);

	tree_save(u->t, b, u->games / 100);

	tree_done(u->t);

	return true;
}

void
uct_dumpbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	u->t = tree_init(b, color);
	tree_load(u->t, b, color, NULL, NULL);
	tree_dump(u->t, 0);
	tree_done(u->t);
}


struct uct *
uct_state_init(char *arg)
{
	struct uct *u = calloc(1, sizeof(struct uct));

	u->debug_level = 1;
	u->games = MC_GAMES;
	u->gamelen = MC_GAMELEN;
	u->expand_p = 2;
	u->dumpthres = 500;

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
			} else if (!strcasecmp(optname, "radar_d") && optval) {
				/* For 19x19, it is good idea to set this to 3. */
				u->radar_d = atoi(optval);
			} else if (!strcasecmp(optname, "dumpthres") && optval) {
				u->dumpthres = atoi(optval);
			} else if (!strcasecmp(optname, "playout_amaf")) {
				/* Whether to include random playout moves in
				 * AMAF as well. (Otherwise, only tree moves
				 * are included in AMAF. Of course makes sense
				 * only in connection with an AMAF policy.) */
				u->playout_amaf = true;
			} else if (!strcasecmp(optname, "policy") && optval) {
				char *policyarg = strchr(optval, ':');
				if (policyarg)
					*policyarg++ = 0;
				if (!strcasecmp(optval, "ucb1")) {
					u->policy = policy_ucb1_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1tuned")) {
					u->policy = policy_ucb1tuned_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1amaf")) {
					u->policy = policy_ucb1amaf_init(u, policyarg);
				} else {
					fprintf(stderr, "UCT: Invalid tree policy %s\n", optval);
				}
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "old")) {
					u->playout = playout_old_init(playoutarg);
				} else if (!strcasecmp(optval, "moggy")) {
					u->playout = playout_moggy_init(playoutarg);
				} else if (!strcasecmp(optval, "light")) {
					u->playout = playout_light_init(playoutarg);
				} else {
					fprintf(stderr, "UCT: Invalid playout policy %s\n", optval);
				}
			} else {
				fprintf(stderr, "uct: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	u->resign_ratio = 0.2; /* Resign when most games are lost. */
	u->loss_threshold = 0.95; /* Stop reading if after at least 500 playouts this is best value. */
	if (!u->policy)
		u->policy = policy_ucb1_init(u, NULL);

	if (!u->playout)
		u->playout = playout_moggy_init(NULL);
	u->playout->debug_level = u->debug_level;

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
	e->notify_play = uct_notify_play;
	e->data = u;

	return e;
}
