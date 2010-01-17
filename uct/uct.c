#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
static void uct_pondering_stop(struct uct *u);


#define MC_GAMES	80000
#define MC_GAMELEN	MAX_GAMELEN

/* How big proportion of ownermap counts must be of one color to consider
 * the point sure. */
#define GJ_THRES	0.8
/* How many games to consider at minimum before judging groups. */
#define GJ_MINGAMES	500

/* How often to inspect the tree from the main thread to check for playout
 * stop, progress reports, etc. A (struct timespec) initializer. */
#define TREE_BUSYWAIT_INTERVAL { .tv_sec = 0, .tv_nsec = 100*1000000 /* 100ms */ }
/* Once per how many simulations to show a progress report line. */
#define TREE_SIMPROGRESS_INTERVAL 10000


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
uct_pass_is_safe(struct uct *u, struct board *b, enum stone color, bool pass_all_alive)
{
	if (u->ownermap.playouts < GJ_MINGAMES)
		return false;

	struct move_queue mq = { .moves = 0 };
	if (!pass_all_alive)
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

	/* Stop pondering. */
	/* XXX: If we are about to receive multiple 'play' commands,
	 * e.g. in a rengo, we will not ponder during the rest of them. */
	uct_pondering_stop(u);

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

	/* This means the game is probably over, no use pondering on. */
	uct_pondering_stop(u);

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
	uct_pondering_stop(u);
	if (u->t) reset_state(u);
	free(u->ownermap.map);

	free(u->policy);
	free(u->random_policy);
	playout_policy_done(u->playout);
	uct_prior_done(u->prior);
}


/* Pachi threading structure (if uct_playouts_parallel() is used):
 *
 * main thread
 *   |         main(), GTP communication, ...
 *   |         starts and stops the search managed by thread_manager
 *   |
 * thread_manager
 *   |         spawns and collects worker threads
 *   |
 * worker0
 * worker1
 * ...
 * workerK
 *             uct_playouts() loop, doing descend-playout until uct_halt
 *
 * Another way to look at it is by functions (lines denote thread boundaries):
 *
 * | uct_genmove()
 * | uct_search()            (uct_search_start() .. uct_search_stop())
 * | -----------------------
 * | spawn_thread_manager()
 * | -----------------------
 * | spawn_worker()
 * V uct_playouts() */

/* Set in thread manager in case the workers should stop. */
volatile sig_atomic_t uct_halt = 0;
/* ID of the running worker thread. */
__thread int thread_id = -1;
/* ID of the thread manager. */
static pthread_t thread_manager;
static bool thread_manager_running;

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
spawn_worker(void *ctx_)
{
	struct spawn_ctx *ctx = ctx_;
	/* Setup */
	fast_srandom(ctx->seed);
	thread_id = ctx->tid;
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

/* Thread manager, controlling worker threads. It must be called with
 * finish_mutex lock held, but it will unlock it itself before exiting;
 * this is necessary to be completely deadlock-free. */
/* The finish_cond can be signalled for it to stop; in that case,
 * the caller should set finish_thread = -1. */
/* After it is started, it will update mctx->t to point at some tree
 * used for the actual search (matters only for TM_ROOT), on return
 * it will set mctx->games to the number of performed simulations. */
static void *
spawn_thread_manager(void *ctx_)
{
	/* In thread_manager, we use only some of the ctx fields. */
	struct spawn_ctx *mctx = ctx_;
	struct uct *u = mctx->u;
	struct tree *t = mctx->t;
	bool shared_tree = u->parallel_tree;
	fast_srandom(mctx->seed);

	int played_games = 0;
	pthread_t threads[u->threads];
	int joined = 0;

	uct_halt = 0;

	/* Spawn threads... */
	for (int ti = 0; ti < u->threads; ti++) {
		struct spawn_ctx *ctx = malloc(sizeof(*ctx));
		ctx->u = u; ctx->b = mctx->b; ctx->color = mctx->color;
		mctx->t = ctx->t = shared_tree ? t : tree_copy(t);
		ctx->tid = ti; ctx->seed = fast_random(65536) + ti;
		pthread_create(&threads[ti], NULL, spawn_worker, ctx);
		if (UDEBUGL(2))
			fprintf(stderr, "Spawned worker %d\n", ti);
	}

	/* ...and collect them back: */
	while (joined < u->threads) {
		/* Wait for some thread to finish... */
		pthread_cond_wait(&finish_cond, &finish_mutex);
		if (finish_thread < 0) {
			/* Stop-by-caller. Tell the workers to wrap up. */
			uct_halt = 1;
			continue;
		}
		/* ...and gather its remnants. */
		struct spawn_ctx *ctx;
		pthread_join(threads[finish_thread], (void **) &ctx);
		played_games += ctx->games;
		joined++;
		if (!shared_tree) {
			if (ctx->t == mctx->t) mctx->t = t;
			tree_merge(t, ctx->t);
			tree_done(ctx->t);
		}
		free(ctx);
		if (UDEBUGL(2))
			fprintf(stderr, "Joined worker %d\n", finish_thread);
		pthread_mutex_unlock(&finish_serializer);
	}

	pthread_mutex_unlock(&finish_mutex);

	if (!shared_tree)
		tree_normalize(mctx->t, u->threads);

	mctx->games = played_games;
	return mctx;
}

static struct spawn_ctx *
uct_search_start(struct uct *u, struct board *b, enum stone color, struct tree *t)
{
	assert(u->threads > 0);
	assert(!thread_manager_running);

	struct spawn_ctx ctx = { .u = u, .b = b, .color = color, .t = t, .seed = fast_random(65536) };
	static struct spawn_ctx mctx; mctx = ctx;
	pthread_mutex_lock(&finish_mutex);
	pthread_create(&thread_manager, NULL, spawn_thread_manager, &mctx);
	thread_manager_running = true;
	return &mctx;
}

static struct spawn_ctx *
uct_search_stop(void)
{
	assert(thread_manager_running);

	/* Signal thread manager to stop the workers. */
	pthread_mutex_lock(&finish_mutex);
	finish_thread = -1;
	pthread_cond_signal(&finish_cond);
	pthread_mutex_unlock(&finish_mutex);

	/* Collect the thread manager. */
	struct spawn_ctx *pctx;
	thread_manager_running = false;
	pthread_join(thread_manager, (void **) &pctx);
	return pctx;
}


/* Run time-limited MCTS search on foreground. */
static int
uct_search(struct uct *u, struct board *b, enum stone color, struct tree *t, int games)
{
	/* Required games limit as to be seen in the tree root u.playouts. */
	int ngames = games * (u->thread_model == TM_ROOT ? 1 : u->threads);
	/* Number of already played games. */
	int pgames = t->root->u.playouts;
	/* Number of last game with progress print. */
	int last_print = 0;

	struct spawn_ctx *ctx = uct_search_start(u, b, color, t);

	/* The search tree is ctx->t. This is normally == t, but in case of
	 * TM_ROOT, it is one of the trees belonging to the independent
	 * workers. It is important to reference ctx->t directly since the
	 * thread manager will swap the tree pointer asynchronously. */
	/* XXX: This means TM_ROOT support is suboptimal since single stalled
	 * thread can stall the others in case of limiting the search by game
	 * count. However, TM_ROOT just does not deserve any more extra code
	 * right now. */

	/* Now, just periodically poll the search tree. */
	struct timespec busywait_interval = TREE_BUSYWAIT_INTERVAL;
	while (1) {
		nanosleep(&busywait_interval, NULL);

		/* Did we play enough games? */
		int i = ctx->t->root->u.playouts - pgames;
		if (i > ngames)
			break;
		/* Won situation? */
		struct tree_node *best = u->policy->choose(u->policy, ctx->t->root, b, color);
		if (best && ((best->u.playouts >= 2000 && tree_node_get_value(ctx->t, 1, best->u.value) >= u->loss_threshold)
			     || (best->u.playouts >= 500 && tree_node_get_value(ctx->t, 1, best->u.value) >= 0.95)))
			break;

		/* Print progress? */
		if (i - last_print > TREE_SIMPROGRESS_INTERVAL) {
			last_print += TREE_SIMPROGRESS_INTERVAL; // keep the numbers tidy
			uct_progress_status(u, ctx->t, color, last_print);
		}
	}

	ctx = uct_search_stop();
	uct_progress_status(u, t, color, ctx->games);
	return ctx->games;
}


/* Start pondering background with @color to play. */
static void
uct_pondering_start(struct uct *u, struct board *b0, struct tree *t, enum stone color)
{
	if (UDEBUGL(1))
		fprintf(stderr, "Starting to ponder with color %s\n", stone2str(stone_other(color)));

	/* We need a local board copy to ponder upon. */
	struct board *b = malloc(sizeof(*b)); board_copy(b, b0);

	/* *b0 did not have the genmove'd move played yet. */
	struct move m = { t->root->coord, t->root_color };
	int res = board_play(b, &m);
	assert(res >= 0);

	/* Start MCTS manager thread "headless". */
	uct_search_start(u, b, color, t);
}

/* uct_search_stop() frontend for the pondering (non-genmove) mode. */
static void
uct_pondering_stop(struct uct *u)
{
	if (!thread_manager_running)
		return;

	/* Stop the thread manager. */
	struct spawn_ctx *ctx = uct_search_stop();
	if (UDEBUGL(1))
		fprintf(stderr, "Pondering yielded %d games\n", ctx->games);
	free(ctx->b);
}


static coord_t *
uct_genmove(struct engine *e, struct board *b, enum stone color, bool pass_all_alive)
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
	uct_pondering_stop(u);
	prepare_move(e, b, color);
	assert(u->t);

	/* Determine number of simulations. */
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

	/* Perform the Monte Carlo Tree Search! */
	int played_games = uct_search(u, b, color, u->t, games);

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
		if (uct_pass_is_safe(u, b, color, u->pass_all_alive || pass_all_alive)) {
			if (UDEBUGL(0))
				fprintf(stderr, "<Will rather pass, looks safe enough.>\n");
			best->coord = pass;
		}
	}

	tree_promote_node(u->t, best);
	/* After a pass, pondering is harmful for two reasons:
	 * (i) We might keep pondering even when the game is over.
	 * Of course this is the case for opponent resign as well.
	 * (ii) More importantly, the ownermap will get skewed since
	 * the UCT will start cutting off any playouts. */
	if (u->pondering && !is_pass(best->coord)) {
		uct_pondering_start(u, b, u->t, stone_other(color));
	}
	return coord_copy(best->coord);
}


bool
uct_genbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	if (!u->t) prepare_move(e, b, color);
	assert(u->t);

	uct_search(u, b, color, u->t, u->games);

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
	u->mercymin = 0;
	u->expand_p = 2;
	u->dumpthres = 1000;
	u->playout_amaf = true;
	u->playout_amaf_nakade = false;
	u->amaf_prior = false;
	u->max_tree_size = 3072ULL * 1048576;

	if (board_size(b) - 2 >= 19)
		u->dynkomi = 200;
	u->dynkomi_mask = S_BLACK;

	u->threads = 1;
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
			} else if (!strcasecmp(optname, "mercy") && optval) {
				/* Minimal difference of black/white captures
				 * to stop playout - "Mercy Rule". Speeds up
				 * hopeless playouts at the expense of some
				 * accuracy. */
				u->mercymin = atoi(optval);
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
				u->prior = uct_prior_init(optval, b);
			} else if (!strcasecmp(optname, "amaf_prior") && optval) {
				u->amaf_prior = atoi(optval);
			} else if (!strcasecmp(optname, "threads") && optval) {
				/* By default, Pachi will run with only single
				 * tree search thread! */
				u->threads = atoi(optval);
			} else if (!strcasecmp(optname, "thread_model") && optval) {
				if (!strcasecmp(optval, "root")) {
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
			} else if (!strcasecmp(optname, "pondering")) {
				/* Keep searching even during opponent's turn. */
				u->pondering = !optval || atoi(optval);
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
			} else if (!strcasecmp(optname, "max_tree_size") && optval) {
				/* Maximum amount of memory [MiB] consumed by the move tree.
				 * Default is 3072 (3 GiB). Note that if you use TM_ROOT,
				 * this limits size of only one of the trees, not all of them
				 * together. */
				u->max_tree_size = atol(optval) * 1048576;
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

	if (!!u->random_policy_chance ^ !!u->random_policy) {
		fprintf(stderr, "uct: Only one of random_policy and random_policy_chance is set\n");
		exit(1);
	}

	if (!u->prior)
		u->prior = uct_prior_init(NULL, b);

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
