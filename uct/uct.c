#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "gtp.h"
#include "move.h"
#include "mq.h"
#include "playout.h"
#include "playout/moggy.h"
#include "playout/light.h"
#include "random.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg);
static void uct_done_board_state(struct engine *e, struct board *b);


#define MC_GAMES	80000
#define MC_GAMELEN	MAX_GAMELEN

/* How big proportion of ownermap counts must be of one color to consider
 * the point sure. */
#define GJ_THRES	0.8
/* How many games to consider at minimum before judging groups. */
#define GJ_MINGAMES	500


static void
prepare_move(struct engine *e, struct board *b, enum stone color, coord_t promote)
{
	struct uct *u = e->data;
	struct uct_board *ub = b->es;

	if (ub) {
		/* Verify that we don't have stale state from last game. */
		assert(ub->t && b->moves);
		if (color != stone_other(ub->t->root_color)) {
			fprintf(stderr, "Fatal: Non-alternating play detected %d %d\n",
				color, ub->t->root_color);
			exit(1);
		}

	} else {
		/* We need fresh state. */
		b->es = ub = calloc(1, sizeof(*ub));
		ub->t = tree_init(b, color);
		if (u->force_seed)
			fast_srandom(u->force_seed);
		if (UDEBUGL(0))
			fprintf(stderr, "Fresh board with random seed %lu\n", fast_getseed());
		//board_print(b, stderr);
		if (!u->no_book && b->moves < 2)
			tree_load(ub->t, b);
		ub->ownermap.map = malloc(board_size2(b) * sizeof(ub->ownermap.map[0]));
	}

	if (u->dynkomi && u->dynkomi > b->moves && (color & u->dynkomi_mask))
		ub->t->extra_komi = uct_get_extra_komi(u, b);

	ub->ownermap.playouts = 0;
	memset(ub->ownermap.map, 0, board_size2(b) * sizeof(ub->ownermap.map[0]));
}

static void
dead_group_list(struct uct *u, struct board *b, struct move_queue *mq)
{
	struct uct_board *ub = b->es;
	assert(ub);

	struct group_judgement gj;
	gj.thres = GJ_THRES;
	gj.gs = alloca(board_size2(b) * sizeof(gj.gs[0]));
	playout_ownermap_judge_group(b, &ub->ownermap, &gj);

	foreach_point(b) { /* foreach_group, effectively */
		group_t g = group_at(b, c);
		if (!g || g != c) continue;

		assert(gj.gs[g] != GS_NONE);
		if (gj.gs[g] == GS_DEAD)
			mq_add(mq, g);
		/* else we assume the worst - alive. */
	} foreach_point_end;
}

bool
uct_pass_is_safe(struct uct *u, struct board *b, enum stone color)
{
	struct uct_board *ub = b->es;
	assert(ub);
	if (ub->ownermap.playouts < GJ_MINGAMES)
		return false;

	struct move_queue mq = { .moves = 0 };
	dead_group_list(u, b, &mq);
	return pass_is_safe(b, color, &mq);
}


static void
uct_printhook_ownermap(struct board *board, coord_t c, FILE *f)
{
	struct uct_board *ub = board->es;
	if (!ub) return; // no UCT state; can happen e.g. after resign
	const char chr[] = ":XO,"; // dame, black, white, unclear
	const char chm[] = ":xo,";
	char ch = chr[playout_ownermap_judge_point(&ub->ownermap, c, GJ_THRES)];
	if (ch == ',') { // less precise estimate then?
		ch = chm[playout_ownermap_judge_point(&ub->ownermap, c, 0.67)];
	}
	fprintf(f, "%c ", ch);
}

static void
uct_notify_play(struct engine *e, struct board *b, struct move *m)
{
	struct uct *u = e->data;
	struct uct_board *ub = b->es;
	if (!ub) {
		/* No state, no worry. */
		return;
	}

	if (is_resign(m->coord)) {
		/* Reset state. */
		uct_done_board_state(e, b);
		return;
	}

	/* Promote node of the appropriate move to the tree root. */
	assert(ub->t->root);
	if (!tree_promote_at(ub->t, b, m->coord)) {
		if (UDEBUGL(0))
			fprintf(stderr, "Warning: Cannot promote move node! Several play commands in row?\n");
		uct_done_board_state(e, b);
		return;
	}
}

static char *
uct_chat(struct engine *e, struct board *b, char *cmd)
{
	static char reply[1024];
	struct uct_board *ub = b->es;

	cmd += strspn(cmd, " \n\t");
	if (!strncasecmp(cmd, "winrate", 7)) {
		if (!ub)
			return "no game context (yet?)";
		enum stone color = ub->t->root_color;
		struct tree_node *n = ub->t->root;
		snprintf(reply, 1024, "In %d playouts, %s %s can win with %.2f%% probability",
			 n->u.playouts, stone2str(color), coord2sstr(n->coord, b), n->u.value * 100);
		if (abs(ub->t->extra_komi) >= 0.5) {
			sprintf(reply + strlen(reply), ", while self-imposing extra komi %.1f",
				ub->t->extra_komi);
		}
		strcat(reply, ".");
		return reply;
	}
	return NULL;
}

static void
uct_dead_group_list(struct engine *e, struct board *b, struct move_queue *mq)
{
	struct uct *u = e->data;
	struct uct_board *ub = b->es;
	bool mock_state = false;

	if (!ub) {
		/* No state, but we cannot just back out - we might
		 * have passed earlier, only assuming some stones are
		 * dead, and then re-connected, only to lose counting
		 * when all stones are assumed alive. */
		/* Mock up some state and seed the ownermap by few
		 * simulations. */
		prepare_move(e, b, S_BLACK, resign);
		ub = b->es; assert(ub);
		for (int i = 0; i < GJ_MINGAMES; i++)
			uct_playout(u, b, S_BLACK, ub->t);
		mock_state = true;
	}

	dead_group_list(u, b, mq);

	if (mock_state) {
		/* Clean up the mock state in case we will receive
		 * a genmove; we could get a non-alternating-move
		 * error from prepare_move() in that case otherwise. */
		uct_done_board_state(e, b);
	}
}

static void
uct_done_board_state(struct engine *e, struct board *b)
{
	struct uct_board *ub = b->es;
	assert(ub);
	assert(ub->t && ub->ownermap.map);
	tree_done(ub->t);
	free(ub->ownermap.map);
	free(ub);
	b->es = NULL;
}


/* Set in main thread in case the playouts should stop. */
volatile sig_atomic_t uct_halt = 0;

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

static coord_t *
uct_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;

	if (b->superko_violation) {
		fprintf(stderr, "!!! WARNING: SUPERKO VIOLATION OCCURED BEFORE THIS MOVE\n");
		fprintf(stderr, "Maybe you play with situational instead of positional superko?\n");
		fprintf(stderr, "I'm going to ignore the violation, but note that I may miss\n");
		fprintf(stderr, "some moves valid under this ruleset because of this.\n");
		b->superko_violation = false;
	}

	/* Seed the tree. */
	prepare_move(e, b, color, resign);

	struct uct_board *ub = b->es;
	assert(ub);

	int played_games = 0;
	if (!u->threads) {
		played_games = uct_playouts(u, b, color, ub->t);
	} else {
		pthread_t threads[u->threads];
		int joined = 0;
		uct_halt = 0;
		pthread_mutex_lock(&finish_mutex);
		/* Spawn threads... */
		for (int ti = 0; ti < u->threads; ti++) {
			struct spawn_ctx *ctx = malloc(sizeof(*ctx));
			ctx->u = u; ctx->b = b; ctx->color = color;
			ctx->t = tree_copy(ub->t); ctx->tid = ti;
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
			tree_merge(ub->t, ctx->t);
			tree_done(ctx->t);
			free(ctx);
			if (UDEBUGL(2))
				fprintf(stderr, "Joined thread %d\n", finish_thread);
			/* Do not get stalled by slow threads. */
			if (joined >= u->threads / 2)
				uct_halt = 1;
			pthread_mutex_unlock(&finish_serializer);
		}
		pthread_mutex_unlock(&finish_mutex);

		tree_normalize(ub->t, u->threads);
	}

	if (UDEBUGL(2))
		tree_dump(ub->t, u->dumpthres);

	struct tree_node *best = u->policy->choose(u->policy, ub->t->root, b, color);
	if (!best) {
		uct_done_board_state(e, b);
		return coord_copy(pass);
	}
	if (UDEBUGL(0)) {
		uct_progress_status(u, ub->t, color, played_games);
	}
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %s (%d,%d) with score %1.4f (%d/%d:%d games)\n",
			coord2sstr(best->coord, b), coord_x(best->coord, b), coord_y(best->coord, b),
			tree_node_get_value(ub->t, best, u, 1),
			best->u.playouts, ub->t->root->u.playouts, played_games);
	if (tree_node_get_value(ub->t, best, u, 1) < u->resign_ratio && !is_pass(best->coord)) {
		uct_done_board_state(e, b);
		return coord_copy(resign);
	}

	/* If the opponent just passed and we win counting, always
	 * pass as well. */
	if (b->moves > 1 && is_pass(b->last_move.coord) && uct_pass_is_safe(u, b, color)) {
		if (UDEBUGL(0))
			fprintf(stderr, "<Will rather pass, looks safe enough.>\n");
		best->coord = pass;
	}

	tree_promote_node(ub->t, best);
	return coord_copy(best->coord);
}


bool
uct_genbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	struct tree *t = tree_init(b, color);
	tree_load(t, b);

	int i;
	for (i = 0; i < u->games; i++) {
		int result = uct_playout(u, b, color, t);
		if (result == 0) {
			/* Tree descent has hit invalid move. */
			continue;
		}

		if (i > 0 && !(i % 10000)) {
			uct_progress_status(u, t, color, i);
		}
	}
	uct_progress_status(u, t, color, i);

	tree_save(t, b, u->games / 100);

	tree_done(t);

	return true;
}

void
uct_dumpbook(struct engine *e, struct board *b, enum stone color)
{
	struct tree *t = tree_init(b, color);
	tree_load(t, b);
	tree_dump(t, 0);
	tree_done(t);
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

	u->val_scale = 0.02; u->val_points = 20;

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
				 * influenced by win size. Zero means it isn't. */
				u->val_scale = atof(optval);
			} else if (!strcasecmp(optname, "val_points") && optval) {
				/* Maximum size of win to be scaled into game
				 * result value. Zero means boardsize^2. */
				u->val_points = atoi(optval) * 2; // result values are doubled
			} else if (!strcasecmp(optname, "val_extra")) {
				/* If false, the score coefficient will be simply
				 * added to the value, instead of scaling the result
				 * coefficient because of it. */
				u->val_extra = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "banner") && optval) {
				/* Additional banner string. This must come as the
				 * last engine parameter. */
				if (*next) *--next = ',';
				u->banner = strdup(optval);
				break;
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
	e->printhook = uct_printhook_ownermap;
	e->notify_play = uct_notify_play;
	e->chat = uct_chat;
	e->genmove = uct_genmove;
	e->dead_group_list = uct_dead_group_list;
	e->done_board_state = uct_done_board_state;
	e->data = u;

	const char banner[] = "I'm playing UCT. When I'm losing, I will resign, "
		"if I think I win, I play until you pass. "
		"Send me 'winrate' command in private chat to get my assessment of the position.";
	if (!u->banner) u->banner = "";
	e->comment = malloc(sizeof(banner) + strlen(u->banner) + 1);
	sprintf(e->comment, "%s %s", banner, u->banner);

	return e;
}
