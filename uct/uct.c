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
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg);


#define MC_GAMES	40000
#define MC_GAMELEN	400


static void
progress_status(struct uct *u, struct tree *t, enum stone color)
{
	if (!UDEBUGL(0))
		return;

	/* Best move */
	struct tree_node *best = u->policy->choose(u->policy, t->root, t->board, color);
	if (!best) {
		fprintf(stderr, "... No moves left\n");
		return;
	}
	fprintf(stderr, "best %f ", best->value);

	/* Max depth */
	fprintf(stderr, "deepest %d ", t->max_depth);

	/* Best sequence */
	fprintf(stderr, "| seq ");
	for (int depth = 0; depth < 6; depth++) {
		if (best && best->playouts >= 25) {
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
		while ((!can[c] || best->playouts > can[c]->playouts) && ++c < cans);
		for (int d = 0; d < c; d++) can[d] = can[d + 1];
		if (c > 0) can[c - 1] = best;
		best = best->sibling;
	}
	while (--cans >= 0) {
		if (can[cans]) {
			fprintf(stderr, "%3s(%.3f) ", coord2sstr(can[cans]->coord, t->board), can[cans]->value);
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
		amaf->map = calloc(b2.size2 + 1, sizeof(*amaf->map));
		amaf->map++; // -1 is pass
	}

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
			if (n->playouts >= u->expand_p)
				tree_expand_node(t, n, &b2, color, u->radar_d);

			result = play_random_game(&b2, color, u->gamelen, u->playout_amaf ? amaf : NULL, u->playout);
			if (orig_color != color && result >= 0)
				result = !result;
			if (UDEBUGL(7))
				fprintf(stderr, "[%d..%d] %s random playout result %d\n", orig_color, color, coord2sstr(n->coord, t->board), result);
			break;
		}

		n = u->policy->descend(u->policy, t, n, (color == orig_color ? 1 : -1), pass_limit);
		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "-- UCT sent us to [%s] %f\n", coord2sstr(n->coord, t->board), n->value);
		if (amaf && n->coord >= -1)
			amaf->map[n->coord] = color;
		struct move m = { n->coord, color };
		int res = board_play(&b2, &m);
		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */
		    || b2.superko_violation) {
			if (UDEBUGL(6))
				fprintf(stderr, "deleting invalid node %d,%d\n", coord_x(n->coord,b), coord_y(n->coord,b));
			tree_delete_node(t, n);
			result = -1;
			goto end;
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
	if (amaf)
		amaf->color = stone_other(color);
	if (result >= 0)
		u->policy->update(u->policy, n, amaf, result);

end:
	if (amaf) {
		free(amaf->map - 1);
		free(amaf);
	}
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

		if (i > 0 && !(i % 10000)) {
			progress_status(u, u->t, color);
		}

		if (i > 0 && !(i % 1000)) {
			struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
			if (best && best->playouts >= 500 && best->value >= u->loss_threshold)
				break;
		}
	}

	progress_status(u, u->t, color);
	if (UDEBUGL(2))
		tree_dump(u->t);

	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
	if (!best) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(pass);
	}
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %d,%d with score %1.4f (%d games)\n", coord_x(best->coord, b), coord_y(best->coord, b), best->value, u->t->root->playouts);
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
	u->expand_p = 2;

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
				}
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "old")) {
					u->playout = playout_old_init(playoutarg);
				} else if (!strcasecmp(optval, "moggy")) {
					u->playout = playout_moggy_init(playoutarg);
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
		u->playout = playout_old_init(NULL);
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
	e->data = u;

	return e;
}
