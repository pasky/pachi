#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "move.h"
#include "playout.h"
#include "playout/moggy.h"
#include "playout/light.h"
#include "random.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"
#include "uct/uct.h"

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg);


#define MC_GAMES	80000
#define MC_GAMELEN	MAX_GAMELEN


static bool
can_pass(struct board *b, enum stone color)
{
	float score = board_official_score(b);
	if (color == S_BLACK)
		score = -score;
	//fprintf(stderr, "%d score %f\n", color, score);
	return (score > 0);
}

static float
get_extra_komi(struct uct *u, struct board *b)
{
	float extra_komi = board_effective_handicap(b) * (u->dynkomi - b->moves) / u->dynkomi;
	return extra_komi;
}

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
	fprintf(stderr, "best %f ", tree_node_get_value(t, best, u, 1));

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
			fprintf(stderr, "%3s(%.3f) ",
			        coord2sstr(can[cans]->coord, t->board),
				tree_node_get_value(t, can[cans], u, 1));
		} else {
			fprintf(stderr, "           ");
		}
	}

	fprintf(stderr, "\n");
}


static int
uct_leaf_node(struct uct *u, struct board *b, enum stone player_color,
              struct playout_amafmap *amaf,
              struct tree *t, struct tree_node *n, enum stone node_color,
	      char *spaces)
{
	enum stone next_color = stone_other(node_color);
	int parity = (next_color == player_color ? 1 : -1);
	if (n->u.playouts >= u->expand_p) {
		// fprintf(stderr, "expanding %s (%p ^-%p)\n", coord2sstr(n->coord, b), n, n->parent);
		tree_expand_node(t, n, b, next_color, u->radar_d, u, parity);
	}
	if (UDEBUGL(7))
		fprintf(stderr, "%s*-- UCT playout #%d start [%s] %f\n",
			spaces, n->u.playouts, coord2sstr(n->coord, t->board),
			tree_node_get_value(t, n, u, parity));

	int result = play_random_game(b, next_color, u->gamelen, u->playout_amaf ? amaf : NULL, NULL, u->playout);
	if (next_color == S_WHITE) {
		/* We need the result from black's perspective. */
		result = - result;
	}
	if (UDEBUGL(7))
		fprintf(stderr, "%s -- [%d..%d] %s random playout result %d\n",
		        spaces, player_color, next_color, coord2sstr(n->coord, t->board), result);

	return result;
}

static int
uct_playout(struct uct *u, struct board *b, enum stone player_color, struct tree *t)
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
	enum stone node_color = stone_other(player_color);
	assert(node_color == t->root_color);

	int result;
	int pass_limit = (board_size(&b2) - 2) * (board_size(&b2) - 2) / 2;
	int passes = is_pass(b->last_move.coord) && b->moves > 0;

	/* debug */
	int depth = 0;
	static char spaces[] = "\0                                                      ";
	/* /debug */
	if (UDEBUGL(8))
		fprintf(stderr, "--- UCT walk with color %d\n", player_color);

	while (!tree_leaf_node(n) && passes < 2) {
		spaces[depth++] = ' '; spaces[depth] = 0;

		/* Parity is chosen already according to the child color, since
		 * it is applied to children. */
		node_color = stone_other(node_color);
		int parity = (node_color == player_color ? 1 : -1);
		n = u->policy->descend(u->policy, t, n, parity, pass_limit);

		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "%s+-- UCT sent us to [%s:%d] %f\n",
			        spaces, coord2sstr(n->coord, t->board), n->coord,
				tree_node_get_value(t, n, u, parity));

		assert(n->coord >= -1);
		if (amaf && !is_pass(n->coord)) {
			if (amaf->map[n->coord] == S_NONE || amaf->map[n->coord] == node_color) {
				amaf->map[n->coord] = node_color;
			} else { // XXX: Respect amaf->record_nakade
				amaf_op(amaf->map[n->coord], +);
			}
			amaf->game[amaf->gamelen].coord = n->coord;
			amaf->game[amaf->gamelen].color = node_color;
			amaf->gamelen++;
			assert(amaf->gamelen < sizeof(amaf->game) / sizeof(amaf->game[0]));
		}

		struct move m = { n->coord, node_color };
		int res = board_play(&b2, &m);

		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */
		    || b2.superko_violation) {
			if (UDEBUGL(3)) {
				for (struct tree_node *ni = n; ni; ni = ni->parent)
					fprintf(stderr, "%s<%lld> ", coord2sstr(ni->coord, t->board), ni->hash);
				fprintf(stderr, "deleting invalid %s node %d,%d res %d group %d spk %d\n",
				        stone2str(node_color), coord_x(n->coord,b), coord_y(n->coord,b),
					res, group_at(&b2, m.coord), b2.superko_violation);
			}
			tree_delete_node(t, n);
			result = 0;
			goto end;
		}

		if (is_pass(n->coord))
			passes++;
		else
			passes = 0;
	}

	if (amaf) {
		amaf->game_baselen = amaf->gamelen;
		amaf->record_nakade = u->playout_amaf_nakade;
	}

	if (u->dynkomi > b2.moves && (player_color & u->dynkomi_mask))
		b2.komi += get_extra_komi(u, &b2);

	if (passes >= 2) {
		float score = board_official_score(&b2);
		/* Result from black's perspective (no matter who
		 * the player; black's perspective is always
		 * what the tree stores. */
		result = - (score * 2);

		if (UDEBUGL(5))
			fprintf(stderr, "[%d..%d] %s p-p scoring playout result %d (W %f)\n",
				player_color, node_color, coord2sstr(n->coord, t->board), result, score);
		if (UDEBUGL(6))
			board_print(&b2, stderr);

	} else { assert(tree_leaf_node(n));
		result = uct_leaf_node(u, &b2, player_color, amaf, t, n, node_color, spaces);
	}

	if (amaf && u->playout_amaf_cutoff) {
		int cutoff = amaf->game_baselen;
		cutoff += (amaf->gamelen - amaf->game_baselen) * u->playout_amaf_cutoff / 100;
		/* Now, reconstruct the amaf map. */
		memset(amaf->map, 0, board_size2(&b2) * sizeof(*amaf->map));
		for (int i = 0; i < cutoff; i++) {
			coord_t coord = amaf->game[i].coord;
			enum stone color = amaf->game[i].color;
			if (amaf->map[coord] == S_NONE || amaf->map[coord] == color) {
				amaf->map[coord] = color;
			/* Nakade always recorded for in-tree part */
			} else if (amaf->record_nakade || i <= amaf->game_baselen) {
				amaf_op(amaf->map[n->coord], +);
			}
		}
	}

	assert(n == t->root || n->parent);
	if (result != 0) {
		float rval = result > 0;
		if (u->val_scale) {
			float sval = (float) abs(result) / u->val_points;
			sval = sval > 1 ? 1 : sval;
			if (result < 0) sval = 1 - sval;
			if (u->val_extra)
				rval += u->val_scale * sval;
			else
				rval = (1 - u->val_scale) * rval + u->val_scale * sval;
			// fprintf(stderr, "score %d => sval %f, rval %f\n", result, sval, rval);
		}
		u->policy->update(u->policy, t, n, node_color, player_color, amaf, rval);
	}

end:
	if (amaf) {
		free(amaf->map - 1);
		free(amaf);
	}
	board_done_noalloc(&b2);
	return result;
}

static void
prepare_move(struct engine *e, struct board *b, enum stone color, coord_t promote)
{
	struct uct *u = e->data;

	if (u->t && (!b->moves || color != stone_other(u->t->root_color))) {
		/* Stale state from last game */
		tree_done(u->t);
		u->t = NULL;
	}

	if (!u->t) {
		u->t = tree_init(b, color);
		if (u->force_seed)
			fast_srandom(u->force_seed);
		if (UDEBUGL(0))
			fprintf(stderr, "Fresh board with random seed %lu\n", fast_getseed());
		//board_print(b, stderr);
		if (!u->no_book && b->moves < 2)
			tree_load(u->t, b);
	}

	/* XXX: We hope that the opponent didn't suddenly play
	 * several moves in the row. */
	if (!is_resign(promote) && !tree_promote_at(u->t, b, promote)) {
		if (UDEBUGL(2))
			fprintf(stderr, "<cannot find node to promote>\n");
		/* Reset tree */
		tree_done(u->t);
		u->t = tree_init(b, color);
	}

	if (u->dynkomi && u->dynkomi > b->moves && (color & u->dynkomi_mask))
		u->t->extra_komi = get_extra_komi(u, b);
}

/* Set in main thread in case the playouts should stop. */
static volatile sig_atomic_t halt = 0;

static int
uct_playouts(struct uct *u, struct board *b, enum stone color, struct tree *t)
{
	int i, games = u->games;
	if (t->root->children)
		games -= t->root->u.playouts / 1.5;
	/* else this is highly read-out but dead-end branch of opening book;
	 * we need to start from scratch; XXX: Maybe actually base the readout
	 * count based on number of playouts of best node? */
	for (i = 0; i < games; i++) {
		int result = uct_playout(u, b, color, t);
		if (result == 0) {
			/* Tree descent has hit invalid move. */
			continue;
		}

		if (i > 0 && !(i % 10000)) {
			progress_status(u, t, color, i);
		}

		if (i > 0 && !(i % 500)) {
			struct tree_node *best = u->policy->choose(u->policy, t->root, b, color);
			if (best && ((best->u.playouts >= 2000 && tree_node_get_value(t, best, u, 1) >= u->loss_threshold)
			             || (best->u.playouts >= 500 && tree_node_get_value(t, best, u, 1) >= 0.95)))
				break;
		}

		if (halt) {
			if (UDEBUGL(2))
				fprintf(stderr, "<halting early, %d games skipped>\n", games - i);
			break;
		}
	}

	progress_status(u, t, color, i);
	if (UDEBUGL(3))
		tree_dump(t, u->dumpthres);
	return i;
}

static pthread_mutex_t finish_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;
static volatile int finish_thread;
static pthread_mutex_t finish_serializer = PTHREAD_MUTEX_INITIALIZER;

struct spawn_ctx {
	int tid;
	struct uct *u;
	struct board *b;
	enum stone color;
	struct tree *t;
	unsigned long seed;
	int games;
};

static void *
spawn_helper(void *ctx_)
{
	struct spawn_ctx *ctx = ctx_;
	/* Setup */
	fast_srandom(ctx->seed);
	/* Run */
	ctx->games = uct_playouts(ctx->u, ctx->b, ctx->color, ctx->t);
	/* Finish */
	pthread_mutex_lock(&finish_serializer);
	pthread_mutex_lock(&finish_mutex);
	finish_thread = ctx->tid;
	pthread_cond_signal(&finish_cond);
	pthread_mutex_unlock(&finish_mutex);
	return ctx;
}

static void
uct_notify_play(struct engine *e, struct board *b, struct move *m)
{
	prepare_move(e, b, m->color, m->coord);
}

static coord_t *
uct_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;

	/* Seed the tree. */
	prepare_move(e, b, color, resign);

	if (b->superko_violation) {
		fprintf(stderr, "!!! WARNING: SUPERKO VIOLATION OCCURED BEFORE THIS MOVE\n");
		fprintf(stderr, "Maybe you play with situational instead of positional superko?\n");
		fprintf(stderr, "I'm going to ignore the violation, but note that I may miss\n");
		fprintf(stderr, "some moves valid under this ruleset because of this.\n");
		b->superko_violation = false;
	}

	/* If the opponent just passes and we win counting, just
	 * pass as well. */
	if (b->moves > 1 && is_pass(b->last_move.coord) && can_pass(b, color))
		return coord_copy(pass);

	int played_games = 0;
	if (!u->threads) {
		played_games = uct_playouts(u, b, color, u->t);
	} else {
		pthread_t threads[u->threads];
		int joined = 0;
		halt = 0;
		pthread_mutex_lock(&finish_mutex);
		/* Spawn threads... */
		for (int ti = 0; ti < u->threads; ti++) {
			struct spawn_ctx *ctx = malloc(sizeof(*ctx));
			ctx->u = u; ctx->b = b; ctx->color = color;
			ctx->t = tree_copy(u->t); ctx->tid = ti;
			ctx->seed = fast_random(65536) + ti;
			pthread_create(&threads[ti], NULL, spawn_helper, ctx);
			if (UDEBUGL(2))
				fprintf(stderr, "Spawned thread %d\n", ti);
		}
		/* ...and collect them back: */
		while (joined < u->threads) {
			/* Wait for some thread to finish... */
			pthread_cond_wait(&finish_cond, &finish_mutex);
			/* ...and gather its remnants. */
			struct spawn_ctx *ctx;
			pthread_join(threads[finish_thread], (void **) &ctx);
			played_games += ctx->games;
			joined++;
			tree_merge(u->t, ctx->t);
			tree_done(ctx->t);
			free(ctx);
			if (UDEBUGL(2))
				fprintf(stderr, "Joined thread %d\n", finish_thread);
			/* Do not get stalled by slow threads. */
			if (joined >= u->threads / 2)
				halt = 1;
			pthread_mutex_unlock(&finish_serializer);
		}
		pthread_mutex_unlock(&finish_mutex);

		tree_normalize(u->t, u->threads);
	}

	if (UDEBUGL(2))
		tree_dump(u->t, u->dumpthres);

	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
	if (!best) {
		tree_done(u->t); u->t = NULL;
		return coord_copy(pass);
	}
	if (UDEBUGL(0))
		progress_status(u, u->t, color, played_games);
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %s (%d,%d) with score %1.4f (%d/%d:%d games)\n",
			coord2sstr(best->coord, b), coord_x(best->coord, b), coord_y(best->coord, b),
			tree_node_get_value(u->t, best, u, 1),
			best->u.playouts, u->t->root->u.playouts, played_games);
	if (tree_node_get_value(u->t, best, u, 1) < u->resign_ratio && !is_pass(best->coord)) {
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
	tree_load(u->t, b);

	int i;
	for (i = 0; i < u->games; i++) {
		int result = uct_playout(u, b, color, u->t);
		if (result == 0) {
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
	tree_load(u->t, b);
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
	u->dumpthres = 1000;
	u->playout_amaf = true;
	u->playout_amaf_nakade = false;
	u->amaf_prior = true;
	u->dynkomi_mask = S_WHITE | S_BLACK;

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
				/* with-without: 55.5% (+-4.1) */
				if (optval && *optval == '0')
					u->playout_amaf = false;
				else
					u->playout_amaf = true;
			} else if (!strcasecmp(optname, "playout_amaf_nakade")) {
				/* Whether to include nakade moves from playouts
				 * in the AMAF statistics; this tends to nullify
				 * the playout_amaf effect by adding too much
				 * noise. */
				if (optval && *optval == '0')
					u->playout_amaf_nakade = false;
				else
					u->playout_amaf_nakade = true;
			} else if (!strcasecmp(optname, "playout_amaf_cutoff") && optval) {
				/* Keep only first N% of playout stage AMAF
				 * information. */
				u->playout_amaf_cutoff = atoi(optval);
			} else if (!strcasecmp(optname, "policy") && optval) {
				char *policyarg = strchr(optval, ':');
				if (policyarg)
					*policyarg++ = 0;
				if (!strcasecmp(optval, "ucb1")) {
					u->policy = policy_ucb1_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1amaf")) {
					u->policy = policy_ucb1amaf_init(u, policyarg);
				} else {
					fprintf(stderr, "UCT: Invalid tree policy %s\n", optval);
				}
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "moggy")) {
					u->playout = playout_moggy_init(playoutarg);
				} else if (!strcasecmp(optval, "light")) {
					u->playout = playout_light_init(playoutarg);
				} else {
					fprintf(stderr, "UCT: Invalid playout policy %s\n", optval);
				}
			} else if (!strcasecmp(optname, "prior") && optval) {
				u->prior = uct_prior_init(optval);
			} else if (!strcasecmp(optname, "amaf_prior") && optval) {
				u->amaf_prior = atoi(optval);
			} else if (!strcasecmp(optname, "threads") && optval) {
				u->threads = atoi(optval);
			} else if (!strcasecmp(optname, "force_seed") && optval) {
				u->force_seed = atoi(optval);
			} else if (!strcasecmp(optname, "no_book")) {
				u->no_book = true;
			} else if (!strcasecmp(optname, "dynkomi")) {
				/* Dynamic komi in handicap game; linearly
				 * decreases to basic settings until move
				 * #optval. */
				u->dynkomi = optval ? atoi(optval) : 150;
			} else if (!strcasecmp(optname, "dynkomi_mask") && optval) {
				/* Bitmask of colors the player must be
				 * for dynkomi be applied; you may want
				 * to use dynkomi_mask=1 to limit dynkomi
				 * only to games where Pachi is black. */
				u->dynkomi_mask = atoi(optval);
			} else if (!strcasecmp(optname, "val_scale") && optval) {
				/* How much of the game result value should be
				 * influenced by win size. */
				u->val_scale = atof(optval);
			} else if (!strcasecmp(optname, "val_points") && optval) {
				/* Maximum size of win to be scaled into game
				 * result value. */
				u->val_points = atoi(optval) * 2; // result values are doubled
			} else if (!strcasecmp(optname, "val_extra")) {
				/* If false, the score coefficient will be simply
				 * added to the value, instead of scaling the result
				 * coefficient because of it. */
				u->val_extra = !optval || atoi(optval);
			} else {
				fprintf(stderr, "uct: Invalid engine argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	u->resign_ratio = 0.2; /* Resign when most games are lost. */
	u->loss_threshold = 0.85; /* Stop reading if after at least 5000 playouts this is best value. */
	if (!u->policy)
		u->policy = policy_ucb1amaf_init(u, NULL);

	if (!u->prior)
		u->prior = uct_prior_init(NULL);

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
	e->comment = "I'm playing UCT. When we both pass, I will consider all the stones on the board alive. If you are reading this, write 'yes'. Please capture all dead stones before passing; it will not cost you points (area scoring is used).";
	e->genmove = uct_genmove;
	e->notify_play = uct_notify_play;
	e->data = u;

	return e;
}
