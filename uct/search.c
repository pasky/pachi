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
#include "distributed/distributed.h"
#include "move.h"
#include "random.h"
#include "timeinfo.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"


/* Default number of simulations to perform per move.
 * Note that this is now in total over all threads! (Unless TM_ROOT.) */
#define MC_GAMES	80000
static const struct time_info default_ti = {
	.period = TT_MOVE,
	.dim = TD_GAMES,
	.len = { .games = MC_GAMES },
};

/* Once per how many simulations (per thread) to show a progress report line. */
#define TREE_SIMPROGRESS_INTERVAL 10000

/* When terminating UCT search early, the safety margin to add to the
 * remaining playout number estimate when deciding whether the result can
 * still change. */
#define PLAYOUT_DELTA_SAFEMARGIN 1000


/* Pachi threading structure:
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
bool thread_manager_running;

static pthread_mutex_t finish_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;
static volatile int finish_thread;
static pthread_mutex_t finish_serializer = PTHREAD_MUTEX_INITIALIZER;

static void *
spawn_worker(void *ctx_)
{
	struct uct_thread_ctx *ctx = ctx_;
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
	struct uct_thread_ctx *mctx = ctx_;
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
		struct uct_thread_ctx *ctx = malloc2(sizeof(*ctx));
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
		struct uct_thread_ctx *ctx;
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


/*** THREAD MANAGER end */

/*** Search infrastructure: */


int
uct_search_games(struct uct_search_state *s)
{
	return s->ctx->t->root->u.playouts;
}

void
uct_search_start(struct uct *u, struct board *b, enum stone color,
		 struct tree *t, struct time_info *ti,
		 struct uct_search_state *s)
{
	/* Set up search state. */
	s->base_playouts = s->last_dynkomi = s->last_print = t->root->u.playouts;
	s->print_interval = TREE_SIMPROGRESS_INTERVAL * (u->thread_model == TM_ROOT ? 1 : u->threads);
	s->print_fullmem = false;

	if (ti) {
		if (ti->period == TT_NULL) *ti = default_ti;
		time_stop_conditions(ti, b, u->fuseki_end, u->yose_start, &s->stop);
	}

	/* Fire up the tree search thread manager, which will in turn
	 * spawn the searching threads. */
	assert(u->threads > 0);
	assert(!thread_manager_running);
	static struct uct_thread_ctx mctx;
	mctx = (struct uct_thread_ctx) { .u = u, .b = b, .color = color, .t = t, .seed = fast_random(65536) };
	s->ctx = &mctx;
	pthread_mutex_lock(&finish_mutex);
	pthread_create(&thread_manager, NULL, spawn_thread_manager, s->ctx);
	thread_manager_running = true;
}

struct uct_thread_ctx *
uct_search_stop(void)
{
	assert(thread_manager_running);

	/* Signal thread manager to stop the workers. */
	pthread_mutex_lock(&finish_mutex);
	finish_thread = -1;
	pthread_cond_signal(&finish_cond);
	pthread_mutex_unlock(&finish_mutex);

	/* Collect the thread manager. */
	struct uct_thread_ctx *pctx;
	thread_manager_running = false;
	pthread_join(thread_manager, (void **) &pctx);
	return pctx;
}


void
uct_search_progress(struct uct *u, struct board *b, enum stone color,
		    struct tree *t, struct time_info *ti,
		    struct uct_search_state *s, int i)
{
	struct uct_thread_ctx *ctx = s->ctx;

	/* Adjust dynkomi? */
	if (ctx->t->use_extra_komi && u->dynkomi->permove
	    && u->dynkomi_interval
	    && i > s->last_dynkomi + u->dynkomi_interval) {
		s->last_dynkomi += u->dynkomi_interval;
		float old_dynkomi = ctx->t->extra_komi;
		ctx->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, ctx->t);
		if (UDEBUGL(3) && old_dynkomi != ctx->t->extra_komi)
			fprintf(stderr, "dynkomi adjusted (%f -> %f)\n",
				old_dynkomi, ctx->t->extra_komi);
	}

	/* Print progress? */
	if (i - s->last_print > s->print_interval) {
		s->last_print += s->print_interval; // keep the numbers tidy
		uct_progress_status(u, ctx->t, color, s->last_print);
	}

	if (!s->print_fullmem && ctx->t->nodes_size > u->max_tree_size) {
		if (UDEBUGL(2))
			fprintf(stderr, "memory limit hit (%lu > %lu)\n",
				ctx->t->nodes_size, u->max_tree_size);
		s->print_fullmem = true;
	}
}


/* Determine whether we should terminate the search early. */
static bool
uct_search_stop_early(struct uct *u, struct tree *t, struct board *b,
		struct time_info *ti, struct time_stop *stop,
		struct tree_node *best, struct tree_node *best2,
		int played)
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
		double pps = ((double)played) / elapsed;
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

/* Determine whether we should terminate the search later than expected. */
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

bool
uct_search_check_stop(struct uct *u, struct board *b, enum stone color,
		      struct tree *t, struct time_info *ti,
		      struct uct_search_state *s, int i)
{
	struct uct_thread_ctx *ctx = s->ctx;

	/* Never consider stopping if we played too few simulations.
	 * Maybe we risk losing on time when playing in super-extreme
	 * time pressure but the tree is going to be just too messed
	 * up otherwise - we might even play invalid suicides or pass
	 * when we mustn't. */
	if (i < GJ_MINGAMES)
		return false;

	struct tree_node *best = NULL;
	struct tree_node *best2 = NULL; // Second-best move.
	struct tree_node *bestr = NULL; // best's best child.
	struct tree_node *winner = NULL;

	best = u->policy->choose(u->policy, ctx->t->root, b, color, resign);
	if (best) best2 = u->policy->choose(u->policy, ctx->t->root, b, color, best->coord);

	/* Possibly stop search early if it's no use to try on. */
	int played = u->played_all + i - s->base_playouts;
	if (best && uct_search_stop_early(u, ctx->t, b, ti, &s->stop, best, best2, played))
		return true;

	/* Check against time settings. */
	bool desired_done;
	if (ti->dim == TD_WALLTIME) {
		double elapsed = time_now() - ti->len.t.timer_start;
		if (elapsed > s->stop.worst.time) return true;
		desired_done = elapsed > s->stop.desired.time;

	} else { assert(ti->dim == TD_GAMES);
		if (i > s->stop.worst.playouts) return true;
		desired_done = i > s->stop.desired.playouts;
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
		if (!uct_search_keep_looking(u, ctx->t, b, ti, &s->stop, best, best2, bestr, winner, i))
			return true;
	}

	/* TODO: Early break if best->variance goes under threshold
	 * and we already have enough playouts (possibly thanks to book
	 * or to pondering)? */
	return false;
}


struct tree_node *
uct_search_best(struct uct *u, struct board *b, enum stone color,
		bool pass_all_alive, int played_games, int base_playouts,
		coord_t *best_coord)
{
	/* Choose the best move from the tree. */
	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color, resign);
	if (!best) {
		*best_coord = pass;
		return NULL;
	}
	*best_coord = best->coord;
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
	    && u->t->extra_komi < 0.5 /* XXX we assume dynamic komi == we are black */) {
		*best_coord = resign;
		return NULL;
	}

	/* If the opponent just passed and we win counting, always
	 * pass as well. */
	if (b->moves > 1 && is_pass(b->last_move.coord)) {
		/* Make sure enough playouts are simulated. */
		while (u->ownermap.playouts < GJ_MINGAMES)
			uct_playout(u, b, color, u->t);
		if (uct_pass_is_safe(u, b, color, u->pass_all_alive || pass_all_alive)) {
			if (UDEBUGL(0))
				fprintf(stderr, "<Will rather pass, looks safe enough; score %f>\n",
					board_official_score(b, NULL) / 2);
			*best_coord = pass;
			return NULL;
		}
	}
	
	return best;
}
