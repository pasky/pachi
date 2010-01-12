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
#include "playout/elo.h"
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


#define MC_GAMES	80000
#define MC_GAMELEN	MAX_GAMELEN

/* How big proportion of ownermap counts must be of one color to consider
 * the point sure. */
#define GJ_THRES	0.8
/* How many games to consider at minimum before judging groups. */
#define GJ_MINGAMES	500


static void
setup_state(struct uct *u, struct board *b, enum stone color)
{
	u->t = tree_init(b, color);
	if (u->force_seed)
		fast_srandom(u->force_seed);
	if (UDEBUGL(0))
		fprintf(stderr, "Fresh board with random seed %lu\n", fast_getseed());
	//board_print(b, stderr);
	if (!u->no_book && b->moves == 0) {
		assert(color == S_BLACK);
		tree_load(u->t, b);
	}
}

static void
reset_state(struct uct *u)
{
	assert(u->t);
	tree_done(u->t); u->t = NULL;
}

static void
prepare_move(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;

	if (u->t) {
		/* Verify that we have sane state. */
		assert(b->es == u);
		assert(u->t && b->moves);
		if (color != stone_other(u->t->root_color)) {
			fprintf(stderr, "Fatal: Non-alternating play detected %d %d\n",
				color, u->t->root_color);
			exit(1);
		}

	} else {
		/* We need fresh state. */
		b->es = u;
		setup_state(u, b, color);
	}

	if (u->dynkomi && u->dynkomi > b->moves && (color & u->dynkomi_mask))
		u->t->extra_komi = uct_get_extra_komi(u, b);

	u->ownermap.playouts = 0;
	memset(u->ownermap.map, 0, board_size2(b) * sizeof(u->ownermap.map[0]));
}

static void
dead_group_list(struct uct *u, struct board *b, struct move_queue *mq)
{
	struct group_judgement gj;
	gj.thres = GJ_THRES;
	gj.gs = alloca(board_size2(b) * sizeof(gj.gs[0]));
	board_ownermap_judge_group(b, &u->ownermap, &gj);
	groups_of_status(b, &gj, GS_DEAD, mq);
}

bool
uct_pass_is_safe(struct uct *u, struct board *b, enum stone color)
{
	if (u->ownermap.playouts < GJ_MINGAMES)
		return false;

	struct move_queue mq = { .moves = 0 };
	if (!u->pass_all_alive)
		dead_group_list(u, b, &mq);
	return pass_is_safe(b, color, &mq);
}


static void
uct_printhook_ownermap(struct board *board, coord_t c, FILE *f)
{
	struct uct *u = board->es;
	assert(u);
	const char chr[] = ":XO,"; // dame, black, white, unclear
	const char chm[] = ":xo,";
	char ch = chr[board_ownermap_judge_point(&u->ownermap, c, GJ_THRES)];
	if (ch == ',') { // less precise estimate then?
		ch = chm[board_ownermap_judge_point(&u->ownermap, c, 0.67)];
	}
	fprintf(f, "%c ", ch);
}

static char *
uct_notify_play(struct engine *e, struct board *b, struct move *m)
{
	struct uct *u = e->data;
	if (!u->t) {
		/* No state, create one - this is probably game beginning
		 * and we need to load the opening book right now. */
		prepare_move(e, b, m->color);
		assert(u->t);
	}

	if (is_resign(m->coord)) {
		/* Reset state. */
		reset_state(u);
		return NULL;
	}

	/* Promote node of the appropriate move to the tree root. */
	assert(u->t->root);
	if (!tree_promote_at(u->t, b, m->coord)) {
		if (UDEBUGL(0))
			fprintf(stderr, "Warning: Cannot promote move node! Several play commands in row?\n");
		reset_state(u);
		return NULL;
	}

	return NULL;
}

static char *
uct_chat(struct engine *e, struct board *b, char *cmd)
{
	struct uct *u = e->data;
	static char reply[1024];

	cmd += strspn(cmd, " \n\t");
	if (!strncasecmp(cmd, "winrate", 7)) {
		if (!u->t)
			return "no game context (yet?)";
		enum stone color = u->t->root_color;
		struct tree_node *n = u->t->root;
		snprintf(reply, 1024, "In %d*%d playouts, %s %s can win with %.2f%% probability",
			 n->u.playouts, u->threads, stone2str(color), coord2sstr(n->coord, b),
			 tree_node_get_value(u->t, -1, n->u.value) * 100);
		if (abs(u->t->extra_komi) >= 0.5) {
			sprintf(reply + strlen(reply), ", while self-imposing extra komi %.1f",
				u->t->extra_komi);
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
	if (u->pass_all_alive)
		return; // no dead groups

	bool mock_state = false;

	if (!u->t) {
		/* No state, but we cannot just back out - we might
		 * have passed earlier, only assuming some stones are
		 * dead, and then re-connected, only to lose counting
		 * when all stones are assumed alive. */
		/* Mock up some state and seed the ownermap by few
		 * simulations. */
		prepare_move(e, b, S_BLACK); assert(u->t);
		for (int i = 0; i < GJ_MINGAMES; i++)
			uct_playout(u, b, S_BLACK, u->t);
		mock_state = true;
	}

	dead_group_list(u, b, mq);

	if (mock_state) {
		/* Clean up the mock state in case we will receive
		 * a genmove; we could get a non-alternating-move
		 * error from prepare_move() in that case otherwise. */
		reset_state(u);
	}
}

static void
playout_policy_done(struct playout_policy *p)
{
	if (p->done) p->done(p);
	if (p->data) free(p->data);
	free(p);
}

static void
uct_done(struct engine *e)
{
	/* This is called on engine reset, especially when clear_board
	 * is received and new game should begin. */
	struct uct *u = e->data;
	if (u->t) reset_state(u);
	free(u->ownermap.map);

	free(u->policy);
	free(u->random_policy);
	playout_policy_done(u->playout);
	uct_prior_done(u->prior);
}


/* Set in main thread in case the playouts should stop. */
volatile sig_atomic_t uct_halt = 0;
/* ID of the running worker thread. */
__thread int thread_id = -1;

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
	thread_id = ctx->tid;
	/* Run */
	ctx->games = uct_playouts(ctx->u, ctx->b, ctx->color, ctx->t, ctx->games);
	/* Finish */
	pthread_mutex_lock(&finish_serializer);
	pthread_mutex_lock(&finish_mutex);
	finish_thread = ctx->tid;
	pthread_cond_signal(&finish_cond);
	pthread_mutex_unlock(&finish_mutex);
	return ctx;
}

static int
uct_playouts_parallel(struct uct *u, struct board *b, enum stone color, struct tree *t, int games, bool shared_tree)
{
	assert(u->threads > 0);
	assert(u->parallel_tree == shared_tree);

	int played_games = 0;
	pthread_t threads[u->threads];
	int joined = 0;

	uct_halt = 0;
	pthread_mutex_lock(&finish_mutex);
	/* Spawn threads... */
	for (int ti = 0; ti < u->threads; ti++) {
		struct spawn_ctx *ctx = malloc(sizeof(*ctx));
		ctx->u = u; ctx->b = b; ctx->color = color;
		ctx->t = shared_tree ? t : tree_copy(t);
		ctx->tid = ti; ctx->games = games;
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
		if (!shared_tree) {
			tree_merge(t, ctx->t);
			tree_done(ctx->t);
		}
		free(ctx);
		if (UDEBUGL(2))
			fprintf(stderr, "Joined thread %d\n", finish_thread);
		/* Do not get stalled by slow threads. */
		if (joined >= u->threads / 2)
			uct_halt = 1;
		pthread_mutex_unlock(&finish_serializer);
	}
	pthread_mutex_unlock(&finish_mutex);

	if (!shared_tree) {
		/* XXX: Should this be done in shared trees as well? */
		tree_normalize(t, u->threads);
	}
	return played_games;
}


typedef int (*uct_threaded_playouts)(struct uct *u, struct board *b, enum stone color, struct tree *t, int games);

static int
uct_playouts_none(struct uct *u, struct board *b, enum stone color, struct tree *t, int games)
{
	return uct_playouts(u, b, color, t, games);
}

static int
uct_playouts_root(struct uct *u, struct board *b, enum stone color, struct tree *t, int games)
{
	return uct_playouts_parallel(u, b, color, t, games, false);
}

static int
uct_playouts_tree(struct uct *u, struct board *b, enum stone color, struct tree *t, int games)
{
	return uct_playouts_parallel(u, b, color, t, games, true);
}

static uct_threaded_playouts threaded_playouts[] = {
	uct_playouts_none,
	uct_playouts_root,
	uct_playouts_tree,
	uct_playouts_tree,
};


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
	prepare_move(e, b, color);
	assert(u->t);

	/* Run the simulations. */
	int games = u->games;
	if (u->t->root->children) {
		int delta = u->t->root->u.playouts * 2 / 3;
		if (u->parallel_tree) delta /= u->threads;
		games -= delta;
	}
	/* else this is highly read-out but dead-end branch of opening book;
	 * we need to start from scratch; XXX: Maybe actually base the readout
	 * count based on number of playouts of best node? */
	if (games < u->games && UDEBUGL(2))
		fprintf(stderr, "<pre-simulated %d games skipped>\n", u->games - games);

	int played_games;
	played_games = threaded_playouts[u->thread_model](u, b, color, u->t, games);

	if (UDEBUGL(2))
		tree_dump(u->t, u->dumpthres);

	/* Choose the best move from the tree. */
	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color);
	if (!best) {
		reset_state(u);
		return coord_copy(pass);
	}
	if (UDEBUGL(0)) {
		uct_progress_status(u, u->t, color, played_games);
	}
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %s (%d,%d) with score %1.4f (%d/%d:%d games)\n",
			coord2sstr(best->coord, b), coord_x(best->coord, b), coord_y(best->coord, b),
			tree_node_get_value(u->t, 1, best->u.value),
			best->u.playouts, u->t->root->u.playouts, played_games);
	if (tree_node_get_value(u->t, 1, best->u.value) < u->resign_ratio && !is_pass(best->coord)) {
		reset_state(u);
		return coord_copy(resign);
	}

	/* If the opponent just passed and we win counting, always
	 * pass as well. */
	if (b->moves > 1 && is_pass(b->last_move.coord)) {
		/* Make sure enough playouts are simulated. */
		while (u->ownermap.playouts < GJ_MINGAMES)
			uct_playout(u, b, color, u->t);
		if (uct_pass_is_safe(u, b, color)) {
			if (UDEBUGL(0))
				fprintf(stderr, "<Will rather pass, looks safe enough.>\n");
			best->coord = pass;
		}
	}

	tree_promote_node(u->t, best);
	return coord_copy(best->coord);
}


bool
uct_genbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	if (!u->t) prepare_move(e, b, color);
	assert(u->t);

	threaded_playouts[u->thread_model](u, b, color, u->t, u->games);

	tree_save(u->t, b, u->games / 100);

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
uct_state_init(char *arg, struct board *b)
{
	struct uct *u = calloc(1, sizeof(struct uct));

	u->debug_level = 1;
	u->games = MC_GAMES;
	u->gamelen = MC_GAMELEN;
	u->expand_p = 2;
	u->dumpthres = 1000;
	u->playout_amaf = true;
	u->playout_amaf_nakade = false;
	u->amaf_prior = false;

	if (board_size(b) - 2 >= 19)
		u->dynkomi = 200;
	u->dynkomi_mask = S_BLACK;

	u->thread_model = TM_TREEVL;
	u->parallel_tree = true;
	u->virtual_loss = true;

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
			} else if ((!strcasecmp(optname, "policy") || !strcasecmp(optname, "random_policy")) && optval) {
				char *policyarg = strchr(optval, ':');
				struct uct_policy **p = !strcasecmp(optname, "policy") ? &u->policy : &u->random_policy;
				if (policyarg)
					*policyarg++ = 0;
				if (!strcasecmp(optval, "ucb1")) {
					*p = policy_ucb1_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1amaf")) {
					*p = policy_ucb1amaf_init(u, policyarg);
				} else {
					fprintf(stderr, "UCT: Invalid tree policy %s\n", optval);
					exit(1);
				}
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "moggy")) {
					u->playout = playout_moggy_init(playoutarg);
				} else if (!strcasecmp(optval, "light")) {
					u->playout = playout_light_init(playoutarg);
				} else if (!strcasecmp(optval, "elo")) {
					u->playout = playout_elo_init(playoutarg);
				} else {
					fprintf(stderr, "UCT: Invalid playout policy %s\n", optval);
					exit(1);
				}
			} else if (!strcasecmp(optname, "prior") && optval) {
				u->prior = uct_prior_init(optval);
			} else if (!strcasecmp(optname, "amaf_prior") && optval) {
				u->amaf_prior = atoi(optval);
			} else if (!strcasecmp(optname, "threads") && optval) {
				u->threads = atoi(optval);
			} else if (!strcasecmp(optname, "thread_model") && optval) {
				if (!strcasecmp(optval, "none")) {
					/* Turn off multi-threaded reading. */
					u->thread_model = TM_NONE;
				} else if (!strcasecmp(optval, "root")) {
					/* Root parallelization - each thread
					 * does independent search, trees are
					 * merged at the end. */
					u->thread_model = TM_ROOT;
					u->parallel_tree = false;
					u->virtual_loss = false;
				} else if (!strcasecmp(optval, "tree")) {
					/* Tree parallelization - all threads
					 * grind on the same tree. */
					u->thread_model = TM_TREE;
					u->parallel_tree = true;
					u->virtual_loss = false;
				} else if (!strcasecmp(optval, "treevl")) {
					/* Tree parallelization, but also
					 * with virtual losses - this discou-
					 * rages most threads choosing the
					 * same tree branches to read. */
					u->thread_model = TM_TREEVL;
					u->parallel_tree = true;
					u->virtual_loss = true;
				} else {
					fprintf(stderr, "UCT: Invalid thread model %s\n", optval);
					exit(1);
				}
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
				 * to use dynkomi_mask=3 to allow dynkomi
				 * even in games where Pachi is white. */
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
			} else if (!strcasecmp(optname, "root_heuristic") && optval) {
				/* Whether to bias exploration by root node values
				 * (must be supported by the used policy).
				 * 0: Don't.
				 * 1: Do, value = result.
				 * Try to temper the result:
				 * 2: Do, value = 0.5+(result-expected)/2.
				 * 3: Do, value = 0.5+bzz((result-expected)^2). */
				u->root_heuristic = atoi(optval);
			} else if (!strcasecmp(optname, "pass_all_alive")) {
				/* Whether to consider all stones alive at the game
				 * end instead of marking dead groupd. */
				u->pass_all_alive = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "random_policy_chance") && optval) {
				/* If specified (N), with probability 1/N, random_policy policy
				 * descend is used instead of main policy descend; useful
				 * if specified policy (e.g. UCB1AMAF) can make unduly biased
				 * choices sometimes, you can fall back to e.g.
				 * random_policy=UCB1. */
				u->random_policy_chance = atoi(optval);
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
	if (!u->threads) {
		u->thread_model = TM_NONE;
		u->parallel_tree = false;
	}

	if (!!u->random_policy_chance ^ !!u->random_policy) {
		fprintf(stderr, "uct: Only one of random_policy and random_policy_chance is set\n");
		exit(1);
	}

	if (!u->prior)
		u->prior = uct_prior_init(NULL);

	if (!u->playout)
		u->playout = playout_moggy_init(NULL);
	u->playout->debug_level = u->debug_level;

	u->ownermap.map = malloc(board_size2(b) * sizeof(u->ownermap.map[0]));

	/* Some things remain uninitialized for now - the opening book
	 * is not loaded and the tree not set up. */
	/* This will be initialized in setup_state() at the first move
	 * received/requested. This is because right now we are not aware
	 * about any komi or handicap setup and such. */

	return u;
}

struct engine *
engine_uct_init(char *arg, struct board *b)
{
	struct uct *u = uct_state_init(arg, b);
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "UCT Engine";
	e->printhook = uct_printhook_ownermap;
	e->notify_play = uct_notify_play;
	e->chat = uct_chat;
	e->genmove = uct_genmove;
	e->dead_group_list = uct_dead_group_list;
	e->done = uct_done;
	e->data = u;

	const char banner[] = "I'm playing UCT. When I'm losing, I will resign, "
		"if I think I win, I play until you pass. "
		"Anyone can send me 'winrate' in private chat to get my assessment of the position.";
	if (!u->banner) u->banner = "";
	e->comment = malloc(sizeof(banner) + strlen(u->banner) + 1);
	sprintf(e->comment, "%s %s", banner, u->banner);

	return e;
}
