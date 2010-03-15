#include <assert.h>
#include <math.h>
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
#include "timeinfo.h"
#include "distributed/distributed.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg);
static void uct_pondering_stop(struct uct *u);
static void uct_pondering_start(struct uct *u, struct board *b0, struct tree *t, enum stone color);
static char *uct_getstats(struct uct *u, struct board *b, coord_t *c);

/* Default number of simulations to perform per move.
 * Note that this is now in total over all threads! (Unless TM_ROOT.) */
#define MC_GAMES	80000
#define MC_GAMELEN	MAX_GAMELEN
static const struct time_info default_ti = {
	.period = TT_MOVE,
	.dim = TD_GAMES,
	.len = { .games = MC_GAMES },
};

/* How big proportion of ownermap counts must be of one color to consider
 * the point sure. */
#define GJ_THRES	0.8
/* How many games to consider at minimum before judging groups. */
#define GJ_MINGAMES	500

/* How often to inspect the tree from the main thread to check for playout
 * stop, progress reports, etc. (in seconds) */
#define TREE_BUSYWAIT_INTERVAL 0.1 /* 100ms */

/* Once per how many simulations (per thread) to show a progress report line. */
#define TREE_SIMPROGRESS_INTERVAL 10000

/* How often to send stats updates for the distributed engine (in seconds). */
#define STATS_SEND_INTERVAL 0.5

/* When terminating uct_search() early, the safety margin to add to the
 * remaining playout number estimate when deciding whether the result can
 * still change. */
#define PLAYOUT_DELTA_SAFEMARGIN 1000


static void
setup_state(struct uct *u, struct board *b, enum stone color)
{
	u->t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0, u->local_tree_aging);
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
setup_dynkomi(struct uct *u, struct board *b, enum stone to_play)
{
	if (u->t->use_extra_komi && u->dynkomi->permove)
		u->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, u->t);
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

/* This function is called only when running as slave in the distributed version. */
static enum parse_code
uct_notify(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply)
{
	struct uct *u = e->data;

	/* Force resending the whole command history if we are out of sync
	 * but do it only once, not if already getting the history. */
	if ((move_number(id) != b->moves || !b->size)
	    && !reply_disabled(id) && !is_reset(cmd)) {
		if (UDEBUGL(0))
			fprintf(stderr, "Out of sync, id %d, move %d\n", id, b->moves);
		static char buf[128];
		snprintf(buf, sizeof(buf), "out of sync, move %d expected", b->moves);
		*reply = buf;
		return P_DONE_ERROR;
	}
	u->gtp_id = id;
	return reply_disabled(id) ? P_NOREPLY : P_OK;
}

static char *
uct_printhook_ownermap(struct board *board, coord_t c, char *s, char *end)
{
	struct uct *u = board->es;
	assert(u);
	const char chr[] = ":XO,"; // dame, black, white, unclear
	const char chm[] = ":xo,";
	char ch = chr[board_ownermap_judge_point(&u->ownermap, c, GJ_THRES)];
	if (ch == ',') { // less precise estimate then?
		ch = chm[board_ownermap_judge_point(&u->ownermap, c, 0.67)];
	}
	s += snprintf(s, end - s, "%c ", ch);
	return s;
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

	/* Stop pondering, required by tree_promote_at() */
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

	/* If we are a slave in a distributed engine, start pondering once
	 * we know which move we actually played. See uct_genmove() about
	 * the check for pass. */
	if (u->pondering_opt && u->slave && m->color == u->my_color && !is_pass(m->coord))
		uct_pondering_start(u, b, u->t, stone_other(m->color));

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
		snprintf(reply, 1024, "In %d playouts at %d threads, %s %s can win with %.2f%% probability",
			 n->u.playouts, u->threads, stone2str(color), coord2sstr(n->coord, b),
			 tree_node_get_value(u->t, -1, n->u.value) * 100);
		if (u->t->use_extra_komi && abs(u->t->extra_komi) >= 0.5) {
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

	/* Garbage collect the tree by preference when pondering. */
	if (u->pondering && t->nodes && t->nodes_size > t->max_tree_size/2) {
		unsigned long temp_size = (MIN_FREE_MEM_PERCENT * t->max_tree_size) / 100;
		t->root = tree_garbage_collect(t, temp_size, t->root);
	}

	/* Spawn threads... */
	for (int ti = 0; ti < u->threads; ti++) {
		struct spawn_ctx *ctx = malloc(sizeof(*ctx));
		ctx->u = u; ctx->b = mctx->b; ctx->color = mctx->color;
		mctx->t = ctx->t = shared_tree ? t : tree_copy(t);
		ctx->tid = ti; ctx->seed = fast_random(65536) + ti;
		pthread_create(&threads[ti], NULL, spawn_worker, ctx);
		if (UDEBUGL(3))
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
		if (UDEBUGL(3))
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


/* Determine whether we should terminate the search early. */
static bool
uct_search_stop_early(struct uct *u, struct tree *t, struct board *b,
		struct time_info *ti, struct time_stop *stop,
		struct tree_node *best, struct tree_node *best2,
		int base_playouts, int i)
{
	/* Always use at least half the desired time. It is silly
	 * to lose a won game because we played a bad move in 0.1s. */
	double elapsed = 0;
	if (ti->dim == TD_WALLTIME) {
		elapsed = time_now() - ti->len.t.timer_start;
		if (elapsed < 0.5 * stop->desired.time) return false;
	}

	/* Early break in won situation. */
	if (best->u.playouts >= 2000 && tree_node_get_value(t, 1, best->u.value) >= u->loss_threshold)
		return true;
	/* Earlier break in super-won situation. */
	if (best->u.playouts >= 500 && tree_node_get_value(t, 1, best->u.value) >= 0.95)
		return true;

	/* Break early if we estimate the second-best move cannot
	 * catch up in assigned time anymore. We use all our time
	 * if we are in byoyomi with single stone remaining in our
	 * period, however - it's better to pre-ponder. */
	bool time_indulgent = (!ti->len.t.main_time && ti->len.t.byoyomi_stones == 1);
	if (best2 && ti->dim == TD_WALLTIME && !time_indulgent) {
		double remaining = stop->worst.time - elapsed;
		double pps = ((double)i - base_playouts) / elapsed;
		double estplayouts = remaining * pps + PLAYOUT_DELTA_SAFEMARGIN;
		if (best->u.playouts > best2->u.playouts + estplayouts) {
			if (UDEBUGL(2))
				fprintf(stderr, "Early stop, result cannot change: "
					"best %d, best2 %d, estimated %f simulations to go\n",
					best->u.playouts, best2->u.playouts, estplayouts);
			return true;
		}
	}

	return false;
}

/* Determine whether we should terminate the search later. */
static bool
uct_search_keep_looking(struct uct *u, struct tree *t, struct board *b,
		struct time_info *ti, struct time_stop *stop,
		struct tree_node *best, struct tree_node *best2,
		struct tree_node *bestr, struct tree_node *winner, int i)
{
	if (!best) {
		if (UDEBUGL(2))
			fprintf(stderr, "Did not find best move, still trying...\n");
		return true;
	}

	/* Do not waste time if we are winning. Spend up to worst time if
	 * we are unsure, but only desired time if we are sure of winning. */
	float beta = 2 * (tree_node_get_value(t, 1, best->u.value) - 0.5);
	if (ti->dim == TD_WALLTIME && beta > 0) {
		double good_enough = stop->desired.time * beta + stop->worst.time * (1 - beta);
		double elapsed = time_now() - ti->len.t.timer_start;
		if (elapsed > good_enough) return false;
	}

	if (u->best2_ratio > 0) {
		/* Check best/best2 simulations ratio. If the
		 * two best moves give very similar results,
		 * keep simulating. */
		if (best2 && best2->u.playouts
		    && (double)best->u.playouts / best2->u.playouts < u->best2_ratio) {
			if (UDEBUGL(2))
				fprintf(stderr, "Best2 ratio %f < threshold %f\n",
					(double)best->u.playouts / best2->u.playouts,
					u->best2_ratio);
			return true;
		}
	}

	if (u->bestr_ratio > 0) {
		/* Check best, best_best value difference. If the best move
		 * and its best child do not give similar enough results,
		 * keep simulating. */
		if (bestr && bestr->u.playouts
		    && fabs((double)best->u.value - bestr->u.value) > u->bestr_ratio) {
			if (UDEBUGL(2))
				fprintf(stderr, "Bestr delta %f > threshold %f\n",
					fabs((double)best->u.value - bestr->u.value),
					u->bestr_ratio);
			return true;
		}
	}

	if (winner && winner != best) {
		/* Keep simulating if best explored
		 * does not have also highest value. */
		if (UDEBUGL(2))
			fprintf(stderr, "[%d] best %3s [%d] %f != winner %3s [%d] %f\n", i,
				coord2sstr(best->coord, t->board),
				best->u.playouts, tree_node_get_value(t, 1, best->u.value),
				coord2sstr(winner->coord, t->board),
				winner->u.playouts, tree_node_get_value(t, 1, winner->u.value));
		return true;
	}

	/* No reason to keep simulating, bye. */
	return false;
}

/* Run time-limited MCTS search on foreground. */
static int
uct_search(struct uct *u, struct board *b, struct time_info *ti, enum stone color, struct tree *t)
{
	int base_playouts = u->t->root->u.playouts;
	if (UDEBUGL(2) && base_playouts > 0)
		fprintf(stderr, "<pre-simulated %d games skipped>\n", base_playouts);

	/* Set up time conditions. */
	if (ti->period == TT_NULL) *ti = default_ti;
	struct time_stop stop;
	time_stop_conditions(ti, b, u->fuseki_end, u->yose_start, &stop);

	/* Number of last dynkomi adjustment. */
	int last_dynkomi = t->root->u.playouts;
	/* Number of last game with progress print. */
	int last_print = t->root->u.playouts;
	/* Number of simulations to wait before next print. */
	int print_interval = TREE_SIMPROGRESS_INTERVAL * (u->thread_model == TM_ROOT ? 1 : u->threads);
	/* Printed notification about full memory? */
	bool print_fullmem = false;
	/* Absolute time of last distributed stats update. */
	double last_stats_sent = time_now();
	/* Interval between distributed stats updates. */
	double stats_interval = STATS_SEND_INTERVAL;

	struct spawn_ctx *ctx = uct_search_start(u, b, color, t);

	/* The search tree is ctx->t. This is normally == t, but in case of
	 * TM_ROOT, it is one of the trees belonging to the independent
	 * workers. It is important to reference ctx->t directly since the
	 * thread manager will swap the tree pointer asynchronously. */
	/* XXX: This means TM_ROOT support is suboptimal since single stalled
	 * thread can stall the others in case of limiting the search by game
	 * count. However, TM_ROOT just does not deserve any more extra code
	 * right now. */

	struct tree_node *best = NULL;
	struct tree_node *best2 = NULL; // Second-best move.
	struct tree_node *bestr = NULL; // best's best child.
	struct tree_node *winner = NULL;

	double busywait_interval = TREE_BUSYWAIT_INTERVAL;

	/* Now, just periodically poll the search tree. */
	while (1) {
		time_sleep(busywait_interval);
		/* busywait_interval should never be less than desired time, or the
		 * time control is broken. But if it happens to be less, we still search
		 * at least 100ms otherwise the move is completely random. */

		int i = ctx->t->root->u.playouts;

		/* Adjust dynkomi? */
		if (ctx->t->use_extra_komi && u->dynkomi->permove
		    && u->dynkomi_interval
		    && i > last_dynkomi + u->dynkomi_interval) {
			float old_dynkomi = ctx->t->extra_komi;
			ctx->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, ctx->t);
			if (UDEBUGL(3) && old_dynkomi != ctx->t->extra_komi)
				fprintf(stderr, "dynkomi adjusted (%f -> %f)\n", old_dynkomi, ctx->t->extra_komi);
		}

		/* Print progress? */
		if (i - last_print > print_interval) {
			last_print += print_interval; // keep the numbers tidy
			uct_progress_status(u, ctx->t, color, last_print);
		}
		if (!print_fullmem && ctx->t->nodes_size > u->max_tree_size) {
			if (UDEBUGL(2))
				fprintf(stderr, "memory limit hit (%lu > %lu)\n", ctx->t->nodes_size, u->max_tree_size);
			print_fullmem = true;
		}

		/* Never consider stopping if we played too few simulations.
		 * Maybe we risk losing on time when playing in super-extreme
		 * time pressure but the tree is going to be just too messed
		 * up otherwise - we might even play invalid suicides or pass
		 * when we mustn't. */
		if (i < GJ_MINGAMES)
			continue;

		best = u->policy->choose(u->policy, ctx->t->root, b, color, resign);
		if (best) best2 = u->policy->choose(u->policy, ctx->t->root, b, color, best->coord);

		/* Possibly stop search early if it's no use to try on. */
		if (best && uct_search_stop_early(u, ctx->t, b, ti, &stop, best, best2, base_playouts, i))
			break;

		/* Check against time settings. */
		bool desired_done = false;
		double now = time_now();
		if (ti->dim == TD_WALLTIME) {
			double elapsed = now - ti->len.t.timer_start;
			if (elapsed > stop.worst.time) break;
			desired_done = elapsed > stop.desired.time;
			if (stats_interval < 0.1 * stop.desired.time)
				stats_interval = 0.1 * stop.desired.time;

		} else { assert(ti->dim == TD_GAMES);
			if (i > stop.worst.playouts) break;
			desired_done = i > stop.desired.playouts;
		}

		/* We want to stop simulating, but are willing to keep trying
		 * if we aren't completely sure about the winner yet. */
		if (desired_done) {
			if (u->policy->winner && u->policy->evaluate) {
				struct uct_descent descent = { .node = ctx->t->root };
				u->policy->winner(u->policy, ctx->t, &descent);
				winner = descent.node;
			}
			if (best)
				bestr = u->policy->choose(u->policy, best, b, stone_other(color), resign);
			if (!uct_search_keep_looking(u, ctx->t, b, ti, &stop, best, best2, bestr, winner, i))
				break;
		}

		/* TODO: Early break if best->variance goes under threshold and we already
                 * have enough playouts (possibly thanks to book or to pondering)? */

		/* Send new stats for the distributed engine.
		 * End with #\n (not \n\n) to indicate a temporary result. */
		if (u->slave && now - last_stats_sent > stats_interval) {
			printf("=%d %s\n#\n", u->gtp_id, uct_getstats(u, b, NULL));
			fflush(stdout);
			last_stats_sent = now;
		}
	}

	ctx = uct_search_stop();

	if (UDEBUGL(2))
		tree_dump(t, u->dumpthres);
	if (UDEBUGL(0))
		uct_progress_status(u, t, color, ctx->games);

	return ctx->games;
}


/* Start pondering background with @color to play. */
static void
uct_pondering_start(struct uct *u, struct board *b0, struct tree *t, enum stone color)
{
	if (UDEBUGL(1))
		fprintf(stderr, "Starting to ponder with color %s\n", stone2str(stone_other(color)));
	u->pondering = true;

	/* We need a local board copy to ponder upon. */
	struct board *b = malloc(sizeof(*b)); board_copy(b, b0);

	/* *b0 did not have the genmove'd move played yet. */
	struct move m = { t->root->coord, t->root_color };
	int res = board_play(b, &m);
	assert(res >= 0);
	setup_dynkomi(u, b, stone_other(m.color));

	/* Start MCTS manager thread "headless". */
	uct_search_start(u, b, color, t);
}

/* uct_search_stop() frontend for the pondering (non-genmove) mode. */
static void
uct_pondering_stop(struct uct *u)
{
	u->pondering = false;
	if (!thread_manager_running)
		return;

	/* Stop the thread manager. */
	struct spawn_ctx *ctx = uct_search_stop();
	if (UDEBUGL(1)) {
		fprintf(stderr, "(pondering) ");
		uct_progress_status(u, ctx->t, ctx->color, ctx->games);
	}
	free(ctx->b);
}


static coord_t *
uct_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	double start_time = time_now();
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
	u->my_color = color;

	/* How to decide whether to use dynkomi in this game? Since we use
	 * pondering, it's not simple "who-to-play" matter. Decide based on
	 * the last genmove issued. */
	u->t->use_extra_komi = !!(u->dynkomi_mask & color);
	setup_dynkomi(u, b, color);

	if (b->rules == RULES_JAPANESE)
		u->territory_scoring = true;

	/* Make pessimistic assumption about komi for Japanese rules to
	 * avoid losing by 0.5 when winning by 0.5 with Chinese rules.
	 * The rules usually give the same winner if the integer part of komi
	 * is odd so we adjust the komi only if it is even (for a board of
	 * odd size). We are not trying  to get an exact evaluation for rare
	 * cases of seki. For details see http://home.snafu.de/jasiek/parity.html */
	if (u->territory_scoring && (((int)floor(b->komi) + board_size(b)) & 1)) {
		b->komi += (color == S_BLACK ? 1.0 : -1.0);
		if (UDEBUGL(0))
			fprintf(stderr, "Setting komi to %.1f assuming Japanese rules\n",
				b->komi);
	}

	int base_playouts = u->t->root->u.playouts;
	/* Perform the Monte Carlo Tree Search! */
	int played_games = uct_search(u, b, ti, color, u->t);

	/* Choose the best move from the tree. */
	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color, resign);
	if (!best) {
		if (!u->slave) reset_state(u);
		return coord_copy(pass);
	}
	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %s (%d,%d) with score %1.4f (%d/%d:%d/%d games), extra komi %f\n",
			coord2sstr(best->coord, b), coord_x(best->coord, b), coord_y(best->coord, b),
			tree_node_get_value(u->t, 1, best->u.value), best->u.playouts,
			u->t->root->u.playouts, u->t->root->u.playouts - base_playouts, played_games,
			u->t->extra_komi);

	/* Do not resign if we're so short of time that evaluation of best
	 * move is completely unreliable, we might be winning actually.
	 * In this case best is almost random but still better than resign.
	 * Also do not resign if we are getting bad results while actually
	 * giving away extra komi points (dynkomi). */
	if (tree_node_get_value(u->t, 1, best->u.value) < u->resign_ratio
	    && !is_pass(best->coord) && best->u.playouts > GJ_MINGAMES
	    && u->t->extra_komi <= 1 /* XXX we assume dynamic komi == we are black */) {
		if (!u->slave) reset_state(u);
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

	/* If we are a slave in the distributed engine, we'll soon get
	 * a "play" command later telling us which move was chosen,
	 * and pondering now will not gain much. */
	if (!u->slave) {
		tree_promote_node(u->t, &best);

		/* After a pass, pondering is harmful for two reasons:
		 * (i) We might keep pondering even when the game is over.
		 * Of course this is the case for opponent resign as well.
		 * (ii) More importantly, the ownermap will get skewed since
		 * the UCT will start cutting off any playouts. */
		if (u->pondering_opt && !is_pass(best->coord)) {
			uct_pondering_start(u, b, u->t, stone_other(color));
		}
	}
	if (UDEBUGL(2)) {
		double time = time_now() - start_time + 0.000001; /* avoid divide by zero */
		fprintf(stderr, "genmove in %0.2fs (%d games/s, %d games/s/thread)\n",
			time, (int)(played_games/time), (int)(played_games/time/u->threads));
	}
	return coord_copy(best->coord);
}

/* Get stats updates for the distributed engine. Return a buffer
 * with one line "total_playouts threads" then a list of lines
 * "coord playouts value". The last line must not end with \n.
 * If c is not null, add this move with root->playouts weight.
 * This function is called only by the main thread, but may be
 * called while the tree is updated by the worker threads.
 * Keep this code in sync with select_best_move(). */
static char *
uct_getstats(struct uct *u, struct board *b, coord_t *c)
{
	static char reply[10240];
	char *r = reply;
	char *end = reply + sizeof(reply);
	struct tree_node *root = u->t->root;
	r += snprintf(r, end - r, "%d %d", root->u.playouts, u->threads);
	int min_playouts = root->u.playouts / 100;

	// Give a large weight to pass or resign, but still allow other moves.
	if (c)
		r += snprintf(r, end - r, "\n%s %d %.1f", coord2sstr(*c, b), root->u.playouts,
			      (float)is_pass(*c));

	/* We rely on the fact that root->children is set only
	 * after all children are created. */
	for (struct tree_node *ni = root->children; ni; ni = ni->sibling) {
		if (ni->u.playouts <= min_playouts
		    || ni->hints & TREE_HINT_INVALID
		    || is_pass(ni->coord))
			continue;
		char *coord = coord2sstr(ni->coord, b);
		// We return the values as stored in the tree, so from black's view.
		r += snprintf(r, end - r, "\n%s %d %.7f", coord, ni->u.playouts, ni->u.value);
	}
	return reply;
}

static char *
uct_genmoves(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	struct uct *u = e->data;
	assert(u->slave);

	coord_t *c = uct_genmove(e, b, ti, color, pass_all_alive);

	char *reply = uct_getstats(u, b, is_pass(*c) || is_resign(*c) ? c : NULL);
	coord_done(c);
	return reply;
}


bool
uct_genbook(struct engine *e, struct board *b, struct time_info *ti, enum stone color)
{
	struct uct *u = e->data;
	if (!u->t) prepare_move(e, b, color);
	assert(u->t);

	if (ti->dim == TD_GAMES) {
		/* Don't count in games that already went into the book. */
		ti->len.games += u->t->root->u.playouts;
	}
	uct_search(u, b, ti, color, u->t);

	assert(ti->dim == TD_GAMES);
	tree_save(u->t, b, ti->len.games / 100);

	return true;
}

void
uct_dumpbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	struct tree *t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0, u->local_tree_aging);
	tree_load(t, b);
	tree_dump(t, 0);
	tree_done(t);
}


struct uct *
uct_state_init(char *arg, struct board *b)
{
	struct uct *u = calloc(1, sizeof(struct uct));
	bool using_elo = false;

	u->debug_level = debug_level;
	u->gamelen = MC_GAMELEN;
	u->mercymin = 0;
	u->expand_p = 2;
	u->dumpthres = 1000;
	u->playout_amaf = true;
	u->playout_amaf_nakade = false;
	u->amaf_prior = false;
	u->max_tree_size = 3072ULL * 1048576;

	u->dynkomi_mask = S_BLACK;

	u->threads = 1;
	u->thread_model = TM_TREEVL;
	u->parallel_tree = true;
	u->virtual_loss = true;

	u->fuseki_end = 20; // max time at 361*20% = 72 moves (our 36th move, still 99 to play)
	u->yose_start = 40; // (100-40-25)*361/100/2 = 63 moves still to play by us then
	u->bestr_ratio = 0.02;
	// 2.5 is clearly too much, but seems to compensate well for overly stern time allocations.
	// TODO: Further tuning and experiments with better time allocation schemes.
	u->best2_ratio = 2.5;

	u->val_scale = 0.04; u->val_points = 40;

	u->tenuki_d = 4;
	u->local_tree_aging = 2;

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
			} else if (!strcasecmp(optname, "best2_ratio") && optval) {
				/* If set, prolong simulating while
				 * first_best/second_best playouts ratio
				 * is less than best2_ratio. */
				u->best2_ratio = atof(optval);
			} else if (!strcasecmp(optname, "bestr_ratio") && optval) {
				/* If set, prolong simulating while
				 * best,best_best_child values delta
				 * is more than bestr_ratio. */
				u->bestr_ratio = atof(optval);
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
					u->playout = playout_moggy_init(playoutarg, b);
				} else if (!strcasecmp(optval, "light")) {
					u->playout = playout_light_init(playoutarg, b);
				} else if (!strcasecmp(optval, "elo")) {
					u->playout = playout_elo_init(playoutarg, b);
					using_elo = true;
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
				u->pondering_opt = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "fuseki_end") && optval) {
				/* At the very beginning it's not worth thinking
				 * too long because the playout evaluations are
				 * very noisy. So gradually increase the thinking
				 * time up to maximum when fuseki_end percent
				 * of the board has been played.
				 * This only applies if we are not in byoyomi. */
				u->fuseki_end = atoi(optval);
			} else if (!strcasecmp(optname, "yose_start") && optval) {
				/* When yose_start percent of the board has been
				 * played, or if we are in byoyomi, stop spending
				 * more time and spread the remaining time
				 * uniformly.
				 * Between fuseki_end and yose_start, we spend
				 * a constant proportion of the remaining time
				 * on each move. (yose_start should actually
				 * be much earlier than when real yose start,
				 * but "yose" is a good short name to convey
				 * the idea.) */
				u->yose_start = atoi(optval);
			} else if (!strcasecmp(optname, "force_seed") && optval) {
				u->force_seed = atoi(optval);
			} else if (!strcasecmp(optname, "no_book")) {
				u->no_book = true;
			} else if (!strcasecmp(optname, "dynkomi") && optval) {
				/* Dynamic komi approach; there are multiple
				 * ways to adjust komi dynamically throughout
				 * play. We currently support two: */
				char *dynkomiarg = strchr(optval, ':');
				if (dynkomiarg)
					*dynkomiarg++ = 0;
				if (!strcasecmp(optval, "none")) {
					u->dynkomi = uct_dynkomi_init_none(u, dynkomiarg, b);
				} else if (!strcasecmp(optval, "linear")) {
					u->dynkomi = uct_dynkomi_init_linear(u, dynkomiarg, b);
				} else if (!strcasecmp(optval, "adaptive")) {
					u->dynkomi = uct_dynkomi_init_adaptive(u, dynkomiarg, b);
				} else {
					fprintf(stderr, "UCT: Invalid dynkomi mode %s\n", optval);
					exit(1);
				}
			} else if (!strcasecmp(optname, "dynkomi_mask") && optval) {
				/* Bitmask of colors the player must be
				 * for dynkomi be applied; you may want
				 * to use dynkomi_mask=3 to allow dynkomi
				 * even in games where Pachi is white. */
				u->dynkomi_mask = atoi(optval);
			} else if (!strcasecmp(optname, "dynkomi_interval") && optval) {
				/* If non-zero, re-adjust dynamic komi
				 * throughout a single genmove reading,
				 * roughly every N simulations. */
				u->dynkomi_interval = atoi(optval);
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
			} else if (!strcasecmp(optname, "local_tree") && optval) {
				/* Whether to bias exploration by local tree values
				 * (must be supported by the used policy).
				 * 0: Don't.
				 * 1: Do, value = result.
				 * Try to temper the result:
				 * 2: Do, value = 0.5+(result-expected)/2.
				 * 3: Do, value = 0.5+bzz((result-expected)^2).
				 * 4: Do, value = 0.5+sqrt(result-expected)/2. */
				u->local_tree = atoi(optval);
			} else if (!strcasecmp(optname, "tenuki_d") && optval) {
				/* Tenuki distance at which to break the local tree. */
				u->tenuki_d = atoi(optval);
				if (u->tenuki_d > TREE_NODE_D_MAX + 1) {
					fprintf(stderr, "uct: tenuki_d must not be larger than TREE_NODE_D_MAX+1 %d\n", TREE_NODE_D_MAX + 1);
					exit(1);
				}
			} else if (!strcasecmp(optname, "local_tree_aging") && optval) {
				/* How much to reduce local tree values between moves. */
				u->local_tree_aging = atof(optval);
			} else if (!strcasecmp(optname, "local_tree_allseq")) {
				/* By default, only complete sequences are stored
				 * in the local tree. If this is on, also
				 * subsequences starting at each move are stored. */
				u->local_tree_allseq = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "local_tree_playout")) {
				/* Whether to adjust ELO playout probability
				 * distributions according to matched localtree
				 * information. */
				u->local_tree_playout = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "local_tree_pseqroot")) {
				/* By default, when we have no sequence move
				 * to suggest in-playout, we give up. If this
				 * is on, we make probability distribution from
				 * sequences first moves instead. */
				u->local_tree_pseqroot = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "pass_all_alive")) {
				/* Whether to consider all stones alive at the game
				 * end instead of marking dead groupd. */
				u->pass_all_alive = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "territory_scoring")) {
				/* Use territory scoring (default is area scoring).
				 * An explicit kgs-rules command overrides this. */
				u->territory_scoring = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "random_policy_chance") && optval) {
				/* If specified (N), with probability 1/N, random_policy policy
				 * descend is used instead of main policy descend; useful
				 * if specified policy (e.g. UCB1AMAF) can make unduly biased
				 * choices sometimes, you can fall back to e.g.
				 * random_policy=UCB1. */
				u->random_policy_chance = atoi(optval);
			} else if (!strcasecmp(optname, "max_tree_size") && optval) {
				/* Maximum amount of memory [MiB] consumed by the move tree.
				 * For fast_alloc it includes the temp tree used for pruning.
				 * Default is 3072 (3 GiB). Note that if you use TM_ROOT,
				 * this limits size of only one of the trees, not all of them
				 * together. */
				u->max_tree_size = atol(optval) * 1048576;
			} else if (!strcasecmp(optname, "fast_alloc")) {
				u->fast_alloc = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "slave")) {
				/* Act as slave for the distributed engine. */
				u->slave = !optval || atoi(optval);
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
	u->loss_threshold = 0.85; /* Stop reading if after at least 2000 playouts this is best value. */
	if (!u->policy)
		u->policy = policy_ucb1amaf_init(u, NULL);

	if (!!u->random_policy_chance ^ !!u->random_policy) {
		fprintf(stderr, "uct: Only one of random_policy and random_policy_chance is set\n");
		exit(1);
	}

	if (!u->local_tree) {
		/* No ltree aging. */
		u->local_tree_aging = 1.0f;
	}
	if (!using_elo)
		u->local_tree_playout = false;

	if (u->fast_alloc && !u->parallel_tree) {
		fprintf(stderr, "fast_alloc not supported with root parallelization.\n");
		exit(1);
	}
	if (u->fast_alloc)
		u->max_tree_size = (100ULL * u->max_tree_size) / (100 + MIN_FREE_MEM_PERCENT);

	if (!u->prior)
		u->prior = uct_prior_init(NULL, b);

	if (!u->playout)
		u->playout = playout_moggy_init(NULL, b);
	u->playout->debug_level = u->debug_level;

	u->ownermap.map = malloc(board_size2(b) * sizeof(u->ownermap.map[0]));

	if (!u->dynkomi)
		u->dynkomi = uct_dynkomi_init_linear(u, NULL, b);

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
	e->genmoves = uct_genmoves;
	e->dead_group_list = uct_dead_group_list;
	e->done = uct_done;
	e->data = u;
	if (u->slave)
		e->notify = uct_notify;

	const char banner[] = "I'm playing UCT. When I'm losing, I will resign, "
		"if I think I win, I play until you pass. "
		"Anyone can send me 'winrate' in private chat to get my assessment of the position.";
	if (!u->banner) u->banner = "";
	e->comment = malloc(sizeof(banner) + strlen(u->banner) + 1);
	sprintf(e->comment, "%s %s", banner, u->banner);

	return e;
}
