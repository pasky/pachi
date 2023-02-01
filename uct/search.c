#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "joseki/joseki.h"
#include "random.h"
#include "timeinfo.h"
#include "tactics/1lib.h"
#include "tactics/2lib.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"
#include "uct/prior.h"
#include "dcnn.h"
#include "pachi.h"

static int
checked_pthread_join(pthread_t thread, void **retval)
{
	int r = pthread_join(thread, retval);
	if (r)  fail("pthread_join");
	return r;
}

#define pthread_join  checked_pthread_join

/* Default time settings for the UCT engine. In distributed mode, slaves are
 * unlimited by default and all control is done on the master, either in time
 * or with total number of playouts over all slaves. (It is also possible but
 * not recommended to limit only the slaves; the master then decides the move
 * when a majority of slaves have made their choice.) */
static time_info_t default_ti;
static __attribute__((constructor)) void
default_ti_init(void)
{
	time_parse(&default_ti, "10");
}

/* When terminating UCT search early, the safety margin to add to the
 * remaining playout number estimate when deciding whether the result can
 * still change. */
#define PLAYOUT_DELTA_SAFEMARGIN 1000

/* Minimal number of simulations to consider early break. */
#define PLAYOUT_EARLY_BREAK_MIN 5000

/* Minimal time to consider early break (in seconds). */
#define TIME_EARLY_BREAK_MIN 1.0


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
 * | thread_manager()
 * | -----------------------
 * | worker_thread()
 * V uct_playouts() 
 *
 * If we are pondering there is also logger_thread() which checks progress */

volatile sig_atomic_t uct_halt = 0;	/* Set in thread manager in case the workers should stop. */
static pthread_t thread_manager_id;	/* ID of the thread manager. */
bool thread_manager_running;

static pthread_mutex_t finish_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;
static volatile int finish_thread;
static pthread_mutex_t finish_serializer = PTHREAD_MUTEX_INITIALIZER;

static void  uct_expand_next_best_moves(uct_t *u, tree_t *t, board_t *b, enum stone color);
static void *logger_thread(void *ctx_);

static void *
worker_thread(void *ctx_)
{
	/* Setup */
	uct_thread_ctx_t *ctx = (uct_thread_ctx_t*)ctx_;
	uct_search_state_t *s = ctx->s;
	uct_t *u = ctx->u;
	board_t *b = ctx->b;
	enum stone color = ctx->color;
	fast_srandom(ctx->seed);
	int restarted = search_restarted(u);

	/* Fill ownermap for mcowner pattern feature. */
	if (using_patterns()) {
		double time_start = time_now();
		uct_mcowner_playouts(u, b, color);
		
		if (!ctx->tid && !restarted) {
			if (DEBUGL(2))  fprintf(stderr, "mcowner %.2fs\n", time_now() - time_start);
			if (DEBUGL(4))  fprintf(stderr, "\npattern ownermap:\n");
			if (DEBUGL(4))  board_print_ownermap(b, stderr, &u->ownermap);
		}
	}

	/* Stuff that depends on ownermap. */
	if (!ctx->tid && using_patterns()) {
		int dames = ownermap_dames(b, &u->ownermap);
		float score = ownermap_score_est(b, &u->ownermap);
		
		/* Close endgame with japanese rules ? Boost pass prior. */
		if (b->rules == RULES_JAPANESE)
			u->prior->boost_pass = (dames < 10 && fabs(score) <= 3);

		/* Allow pass in uct descent only at the end */
		u->allow_pass = (u->allow_pass && dames < 10);
	}

	/* Expand root node (dcnn). Other threads wait till it's ready. 
	 * For dcnn pondering we also need dcnn values for opponent's best moves. */
	tree_t *t = ctx->t;
	tree_node_t *n = t->root;
	if (!ctx->tid) {
		bool already_have = n->is_expanded;
		enum stone node_color = stone_other(color);
		assert(node_color == t->root_color);
		
		if (tree_leaf_node(n) && !__sync_lock_test_and_set(&n->is_expanded, 1))
			tree_expand_node(t, n, b, color, u, 1);
		if (genmove_pondering(u) && using_dcnn(b))
			uct_expand_next_best_moves(u, t, b, color);
		
		if (DEBUGL(2) && already_have && !restarted) {  /* Show previously computed priors */
			print_joseki_moves(joseki_dict, b, color);
			print_node_prior_best_moves(b, n);
		}
		u->tree_ready = true;
	}
	else while (!u->tree_ready)
		     usleep(100 * 1000);

	/* Run */
	if (!ctx->tid)  s->mcts_time_start = s->last_print_time = time_now();
	ctx->games = uct_playouts(ctx->u, ctx->b, ctx->color, ctx->t, ctx->ti);
	
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
 * used for the actual search, on return
 * it will set mctx->games to the number of performed simulations. */
static void *
thread_manager(void *ctx_)
{
	/* In thread_manager, we use only some of the ctx fields. */
	uct_thread_ctx_t *mctx = (uct_thread_ctx_t*)ctx_;
	uct_t *u = mctx->u;
	tree_t *t = mctx->t;
	fast_srandom(mctx->seed);

	int played_games = 0;
	pthread_t threads[u->threads + 1];
	int joined = 0;

	uct_halt = 0;
	u->tree_ready = false;

	/* Garbage collect the tree by preference when pondering. */
	if (pondering(u) && search_want_gc(u) && t->nodes && tree_gc_needed(u->t))
		tree_garbage_collect(t);
	clear_search_want_gc(u);

	/* Logging thread for pondering */
	if (pondering(u))
		pthread_create(&threads[u->threads], NULL, logger_thread, mctx);
	
	/* Spawn threads... */
	for (int ti = 0; ti < u->threads; ti++) {
		uct_thread_ctx_t *ctx = calloc2(1, uct_thread_ctx_t);
		ctx->u = u; ctx->b = mctx->b; ctx->color = mctx->color;
		mctx->t = ctx->t = t;
		ctx->tid = ti; ctx->seed = fast_random(65536) + ti;
		ctx->ti = mctx->ti;
		ctx->s = mctx->s;
		pthread_attr_t a;
		pthread_attr_init(&a);
		pthread_attr_setstacksize(&a, 1048576);
		pthread_create(&threads[ti], &a, worker_thread, ctx);
		if (UDEBUGL(4))
			fprintf(stderr, "Spawned worker %d\n", ti);
	}

	/* ...and collect them back: */
	while (joined < u->threads) {
		/* Wait for some thread to finish... */
		pthread_cond_wait(&finish_cond, &finish_mutex);
		if (finish_thread < 0) {
			/* Stop-by-caller. Tell the workers to wrap up
			 * and unblock them from terminating. */
			uct_halt = 1;
			/* We need to make sure the workers do not complete
			 * the termination sequence before we get officially
			 * stopped - their wake and the stop wake could get
			 * coalesced. */
			pthread_mutex_unlock(&finish_serializer);
			continue;
		}
		/* ...and gather its remnants. */
		uct_thread_ctx_t *ctx;
		pthread_join(threads[finish_thread], (void **) &ctx);
		played_games += ctx->games;
		joined++;
		free(ctx);
		if (UDEBUGL(4))
			fprintf(stderr, "Joined worker %d\n", finish_thread);
		pthread_mutex_unlock(&finish_serializer);
	}

	if (pondering(u))
		pthread_join(threads[u->threads], NULL);
	
	pthread_mutex_unlock(&finish_mutex);

	mctx->games = played_games;
	return mctx;
}

/* Detached thread to deal with memory full while pondering:
 * Stop search or realloc tree if u->auto_alloc. */
static void *
pondering_fullmem_handler(void *ctx_)
{
	uct_thread_ctx_t *ctx = (uct_thread_ctx_t*)ctx_;
	uct_t *u = ctx->u;
	board_t *b = ctx->b;
	enum stone color = ctx->color;
	uct_search_state_t *s = ctx->s;
	time_info_t *ti = ctx->ti;
	ctx = NULL;

	int r = pthread_detach(pthread_self());  if (r) fail("pthread_detach");

	if (!thread_manager_running)  return NULL;

	if (!u->auto_alloc || !uct_search_realloc_tree(u, b, color, ti, s))
	    uct_pondering_stop(u);
	
	return NULL;
}

/* Logger thread, keeps track of progress when pondering.
 * Similar to uct_search() when pondering. */
static void *
logger_thread(void *ctx_)
{
	uct_thread_ctx_t *ctx = (uct_thread_ctx_t*)ctx_;
	uct_t *u = ctx->u;
	tree_t *t = ctx->t;
	board_t *b = ctx->b;
	enum stone color = ctx->color;
	uct_search_state_t *s = ctx->s;
	time_info_t *ti = ctx->ti;

	while (!uct_halt) {
		time_sleep(TREE_BUSYWAIT_INTERVAL);
		/* TREE_BUSYWAIT_INTERVAL should never be less than desired time, or the
		 * time control is broken. But if it happens to be less, we still search
		 * at least 100ms otherwise the move is completely random. */

		int i = uct_search_games(s);
		/* Print notifications etc. */
		uct_search_progress(u, b, color, t, ti, s, i);
		
		if (s->fullmem) {
			/* Stop search / Realloc tree.
			 * Do it from another thread, would deadlock here. */
			pthread_t tid;
			pthread_create(&tid, NULL, pondering_fullmem_handler, ctx);
			return NULL;
		}		
	}
	return NULL;
}

/* Expand next move node (dcnn pondering) */
static void
uct_expand_next_move(uct_t *u, tree_t *t, board_t *board, enum stone color, coord_t c)
{
	tree_node_t *n = tree_get_node(t->root, c);
	
	board_t b;
	board_copy(&b, board);

	move_t m = move(c, color);
	int res = board_play(&b, &m);
	if (res < 0) goto done;
		
	if (!__sync_lock_test_and_set(&n->is_expanded, 1))
		tree_expand_node(t, n, &b, stone_other(color), u, -1);

 done:  board_done(&b);
}

/* For pondering with dcnn we need dcnn values for next move as well before
 * search starts. Can't evaluate all of them, so guess from prior best moves +
 * genmove's best moves for opponent. If we guess right all is well. If we
 * guess wrong pondering will not be useful for this move, search results
 * will be discarded. */
static void
uct_expand_next_best_moves(uct_t *u, tree_t *t, board_t *b, enum stone color)
{
	assert(using_dcnn(b));
	move_queue_t q;  mq_init(&q);
	
	{  /* Prior best moves (dcnn policy mostly) */
		int nbest = u->dcnn_pondering_prior;
		float best_r[nbest];
		coord_t best_c[nbest];
		get_node_prior_best_moves(t->root, best_c, best_r, nbest);
		assert(t->root->hints & TREE_HINT_DCNN);
		
		for (int i = 0; i < nbest && !is_pass(best_c[i]); i++)
			mq_add(&q, best_c[i], 0);
	}
	
	{  /* Opponent best moves from genmove search */
		int       nbest = u->dcnn_pondering_mcts;
		coord_t *best_c = u->dcnn_pondering_mcts_c;
		for (int i = 0; i < nbest && !is_pass(best_c[i]); i++) {
			mq_add(&q, best_c[i], 0);
			mq_nodup(&q);
		}
	}
	
	if (DEBUGL(2)) {  /* Show guesses. */
		fprintf(stderr, "dcnn eval %s ", stone2str(color));
		for (unsigned int i = 0; i < q.moves; i++)
			fprintf(stderr, "%s ", coord2sstr(q.move[i]));
		fflush(stderr);
	}

	for (unsigned int i = 0; i < q.moves && !uct_halt; i++) { /* Don't hang if genmove comes in. */
		uct_expand_next_move(u, t, b, color, q.move[i]);
		if (DEBUGL(2)) {  fprintf(stderr, ".");  fflush(stderr);  }
	}
	if (DEBUGL(2)) fprintf(stderr, "\n");
}


/*** THREAD MANAGER end */

/*** Search infrastructure: */


int
uct_search_games(uct_search_state_t *s)
{
	return s->ctx->t->root->u.playouts;
}

void
uct_search_start(uct_t *u, board_t *b, enum stone color,
		 tree_t *t, time_info_t *ti,
		 uct_search_state_t *s, int flags)
{
	u->search_flags = flags;
	
	/* Set up search state. */
	s->base_playouts = s->last_dynkomi = s->last_print_playouts = t->root->u.playouts;
	s->fullmem = false;

	/* If restarted timers are already setup, reuse stop condition in s */
	if (ti && !search_restarted(u)) {
		if (ti->type == TT_NULL) {
			*ti = default_ti;
			time_start_timer(ti);
		}
		time_stop_conditions(ti, b, u->fuseki_end, u->yose_start, u->max_maintime_ratio, &s->stop);
	}

	/* Fire up the tree search thread manager, which will in turn
	 * spawn the searching threads. */
	assert(u->threads > 0);
	assert(!thread_manager_running);
	static uct_thread_ctx_t mctx;
	mctx = (uct_thread_ctx_t) { 0, u, b, color, t, fast_random(65536), 0, ti, s };
	s->ctx = &mctx;
	pthread_mutex_lock(&finish_serializer);
	pthread_mutex_lock(&finish_mutex);
	pthread_create(&thread_manager_id, NULL, thread_manager, s->ctx);
	thread_manager_running = true;
}

/* Stop current search. Clears search flags. */
uct_thread_ctx_t *
uct_search_stop(void)
{
	assert(thread_manager_running);
	thread_manager_running = false;

	/* Signal thread manager to stop the workers. */
	pthread_mutex_lock(&finish_mutex);
	finish_thread = -1;
	pthread_cond_signal(&finish_cond);
	pthread_mutex_unlock(&finish_mutex);

	/* Collect the thread manager. */
	uct_thread_ctx_t *pctx;
	pthread_join(thread_manager_id, (void **) &pctx);
	
	uct_t *u = pctx->u;
	uct_search_state_t *s = pctx->s;
	u->mcts_time += time_now() - s->mcts_time_start;
	u->search_flags = 0;  /* Reset search flags */
	
	return pctx;
}

static void
fullmem_warning(uct_t *u, char *msg)
{
	if (UDEBUGL(2))  fprintf(stderr, "%s", msg);
}

/* Stop search, realloc tree and resume search */
int
uct_search_realloc_tree(uct_t *u, board_t *b, enum stone color, time_info_t *ti, uct_search_state_t *s)
{
	size_t old_size = u->tree_size;
	size_t new_size = old_size * 2;
	size_t max_tree_size = (u->max_tree_size_opt ? u->max_tree_size_opt : (size_t)-1);
	size_t max_mem = (u->max_mem ? u->max_mem : (size_t)-1);

	/* Use all available memory if needed but don't bother reallocating for a few % */
	size_t minimum_new_size = old_size + old_size / 10;
	if (new_size > max_tree_size && max_tree_size > minimum_new_size)
		new_size = max_tree_size;
	// XXX max_mem: take pruned_size into account ?
	if ((old_size + new_size) > max_mem && (max_mem - old_size) > minimum_new_size)
		new_size = max_mem - old_size;
	
	/* Don't go over memory limits */
	if (new_size > max_tree_size || (old_size + new_size) > max_mem) {
		fullmem_warning(u, "WARNING: Max memory limit reached, stopping search.\n");
		return 0;
	}
	
	if (UDEBUGL(2)) fprintf(stderr, "Tree memory full, reallocating (%i -> %i Mb)\n",
				(int)(old_size / (1024*1024)), (int)(new_size / (1024*1024)));

	/* Can't simply use tree_realloc(), need to check if we can allocate
	 * memory before stopping search otherwise we can't recover. */
	tree_t *t  = u->t;
	tree_t *t2 = tree_init(stone_other(t->root_color), new_size, tree_hbits(t));
	if (!t2)  return 0;		/* Not enough memory */
	
	int flags = u->search_flags;	/* Save flags ! */
	uct_search_stop();
	
	uct_tree_size_init(u, new_size);
	
	double time_start = time_now();
	tree_copy(t2, t);	assert(t2->root_color == t->root_color);
	tree_replace(t, t2);
	if (UDEBUGL(2)) fprintf(stderr, "tree realloc in %.1fs\n", time_now() - time_start);

	/* Restart search (preserve timers...) */
	s->fullmem = false;
	uct_search_start(u, b, color, u->t, ti, s, flags | UCT_SEARCH_RESTARTED);
	return 1;
}

void
uct_search_progress(uct_t *u, board_t *b, enum stone color,
		    tree_t *t, time_info_t *ti,
		    uct_search_state_t *s, int playouts)
{
	uct_thread_ctx_t *ctx = s->ctx;

	/* Adjust dynkomi? */
	int di = u->dynkomi_interval * u->threads;
	if (ctx->t->use_extra_komi && u->dynkomi->permove
	    && !pondering(u) && di
	    && playouts > s->last_dynkomi + di) {
		s->last_dynkomi += di;
		floating_t old_dynkomi = ctx->t->extra_komi;
		ctx->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, ctx->t);
		if (UDEBUGL(3) && old_dynkomi != ctx->t->extra_komi)
			fprintf(stderr, "dynkomi adjusted (%f -> %f)\n", old_dynkomi, ctx->t->extra_komi);
	}

	/* Print progress ? */
	if (u->reportfreq_time) { /* Time based */
		if (playouts > 100 && time_now() - s->last_print_time > u->reportfreq_time) {
			s->last_print_time = time_now();
			uct_progress_status(u, ctx->t, ctx->b, color, playouts, NULL);
		}
	}
	else		          /* Playouts based */
		if (playouts - s->last_print_playouts > u->reportfreq_playouts) {
			s->last_print_playouts += u->reportfreq_playouts; // keep the numbers tidy
			uct_progress_status(u, ctx->t, ctx->b, color, s->last_print_playouts, NULL);
		}

        if (!s->fullmem && ctx->t->nodes_size > ctx->t->max_tree_size) {
		s->fullmem = true;
		if (!u->auto_alloc)
			fullmem_warning(u, "WARNING: Tree memory limit reached, stopping search.\n");
	}
}


/* Determine whether we should terminate the search early. */
static bool
uct_search_stop_early(uct_t *u, tree_t *t, board_t *b,
		time_info_t *ti, time_stop_t *stop,
		tree_node_t *best, tree_node_t *best2,
		int played, bool fullmem)
{
	/* If the memory is full, stop immediately. Since the tree
	 * cannot grow anymore, some non-well-expanded nodes will
	 * quickly take over with extremely high ratio since the
	 * counters are not properly simulated (just as if we use
	 * non-UCT MonteCarlo). */
	/* (XXX: A proper solution would be to prune the tree
	 * on the spot.) */
	if (fullmem)
		return true;

	/* Think at least 100ms to avoid a random move. This is particularly
	 * important in distributed mode, where this function is called frequently. */
	double elapsed = 0.0;
	if (ti->dim == TD_WALLTIME) {
		elapsed = time_now() - ti->timer_start;
		if (elapsed < TREE_BUSYWAIT_INTERVAL) return false;
	}

	/* Fixed Playouts: Stop early if the second-best move cannot catch up anymore */
	if (ti->can_stop_early && ti->dim == TD_GAMES &&
	    played >= PLAYOUT_EARLY_BREAK_MIN && best2) {
		int total_played = t->root->u.playouts;
		int remaining = stop->worst.playouts - total_played;
		if (remaining > 0 &&
		    best->u.playouts > best2->u.playouts + remaining) {
			if (UDEBUGL(2))  fprintf(stderr, "Early stop, result cannot change\n");
			return true;
		}
	}

	/* Walltime: Stop early if we estimate the second-best move cannot catch up in  
	 * assigned time anymore. If we are in byoyomi with single period remaining
	 * and can do some lookahead, use all our time - it's better to pre-ponder. */
	bool last_byoyomi = (!ti->main_time && ti->byoyomi_stones == 1);
	bool keep_looking = (last_byoyomi && reusing_tree(u, b));
	if (ti->can_stop_early && ti->dim == TD_WALLTIME &&
	    !keep_looking &&
	    played >= PLAYOUT_EARLY_BREAK_MIN && best2) {
		double remaining = stop->worst.time - elapsed;
		double pps = ((double)played) / elapsed;
		double estplayouts = remaining * pps + PLAYOUT_DELTA_SAFEMARGIN;
		if (best->u.playouts > best2->u.playouts + estplayouts) {
			if (UDEBUGL(2))  fprintf(stderr, "Early stop, result cannot change\n");
			if (UDEBUGL(3))  fprintf(stderr, "best %d, best2 %d, estimated %i sims to go (%d/%.1f=%i pps)\n",
						 best->u.playouts, best2->u.playouts, (int)estplayouts, played, elapsed, (int)pps);
			return true;
		}
	}

	/* Early break in won situation. */
	if (best->u.playouts >= PLAYOUT_EARLY_BREAK_MIN
	    && (ti->dim != TD_WALLTIME || elapsed > TIME_EARLY_BREAK_MIN)
	    && tree_node_get_value(t, 1, best->u.value) >= u->sure_win_threshold) {
		return true;
	}

	return false;
}

/* Determine whether we should terminate the search later than expected. */
static bool
uct_search_keep_looking(uct_t *u, tree_t *t, board_t *b,
		time_info_t *ti, time_stop_t *stop,
		tree_node_t *best, tree_node_t *best2,
		tree_node_t *bestr, tree_node_t *winner, int i)
{
	if (!best) {
		if (UDEBUGL(2))
			fprintf(stderr, "Did not find best move, still trying...\n");
		return true;
	}

	/* Do not waste time if we are winning. Spend up to worst time if
	 * we are unsure, but only desired time if we are sure of winning. */
	floating_t beta = 2 * (tree_node_get_value(t, 1, best->u.value) - 0.5);
	if (ti->dim == TD_WALLTIME && beta > 0) {
		double good_enough = stop->desired.time * beta + stop->worst.time * (1 - beta);
		double elapsed = time_now() - ti->timer_start;
		if (elapsed > good_enough) return false;
	}

	if (u->best2_ratio > 0) {
		/* Check best/best2 simulations ratio. If the
		 * two best moves give very similar results,
		 * keep simulating. */
		if (best2 && best2->u.playouts
		    && (double)best->u.playouts / best2->u.playouts < u->best2_ratio) {
			if (UDEBUGL(3))
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
			if (UDEBUGL(3))
				fprintf(stderr, "Bestr delta %f > threshold %f\n",
					fabs((double)best->u.value - bestr->u.value),
					u->bestr_ratio);
			return true;
		}
	}

	if (winner && winner != best) {
		/* Keep simulating if best explored
		 * does not have also highest value. */
		if (UDEBUGL(3))
			fprintf(stderr, "[%d] best %3s [%d] %f != winner %3s [%d] %f\n", i,
				coord2sstr(node_coord(best)),
				best->u.playouts, tree_node_get_value(t, 1, best->u.value),
				coord2sstr(node_coord(winner)),
				winner->u.playouts, tree_node_get_value(t, 1, winner->u.value));
		return true;
	}

	/* No reason to keep simulating, bye. */
	return false;
}

bool
uct_search_check_stop(uct_t *u, board_t *b, enum stone color,
		      tree_t *t, time_info_t *ti,
		      uct_search_state_t *s, int i)
{
	uct_thread_ctx_t *ctx = s->ctx;

	/* Never consider stopping if we played too few simulations.
	 * Maybe we risk losing on time when playing in super-extreme
	 * time pressure but the tree is going to be just too messed
	 * up otherwise - we might even play invalid suicides or pass
	 * when we mustn't. */
	assert(!(ti->dim == TD_GAMES && ti->games < GJ_MINGAMES));
	if (i < GJ_MINGAMES)
		return false;

	tree_node_t *best = NULL;
	tree_node_t *best2 = NULL; // Second-best move.
	tree_node_t *bestr = NULL; // best's best child.
	tree_node_t *winner = NULL;

	best = u->policy->choose(u->policy, ctx->t->root, b, color, resign);
	if (best) best2 = u->policy->choose(u->policy, ctx->t->root, b, color, node_coord(best));

	/* Possibly stop search early if it's no use to try on. */
	int played = played_all(u) + i - s->base_playouts;
	if (best && uct_search_stop_early(u, ctx->t, b, ti, &s->stop, best, best2, played, s->fullmem))
		return true;

	/* Check against time settings. */
	bool desired_done;
	if (ti->dim == TD_WALLTIME) {
		double elapsed = time_now() - ti->timer_start;
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
			uct_descent_t descent = uct_descent(ctx->t->root);
			u->policy->winner(u->policy, ctx->t, &descent);
			winner = descent.node;
		}
		if (best)
			bestr = u->policy->choose(u->policy, best, b, stone_other(color), resign);
		if (!uct_search_keep_looking(u, ctx->t, b, ti, &s->stop, best, best2, bestr, winner, i))
			return true;
	}

	/* TODO: Early break if best->variance goes under threshold
	 * and we already have enough playouts (possibly thanks to tbook
	 * or to pondering)? */
	return false;
}

/* Check pass is safe and save dead groups for later, must use
 * same dead groups at scoring time or we might lose the game.
 * Do this here, uct_pass_is_safe() also called by uct policy */
static bool
uct_search_pass_is_safe(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, char **msg)
{
	move_queue_t dead;
	bool res = uct_pass_is_safe(u, b, color, pass_all_alive, &dead, msg, true);

	if (res) {
		u->dead_groups = dead;
		u->pass_moveno = b->moves + 1;
	}
	return res;
}

static bool
uct_pass_first(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, coord_t coord)
{	
	/* For kgs: when playing chinese must not pass first
	 * in main game phase or cleanup phase can be abused. */
	bool pachi_nopassfirst = (pachi_options()->nopassfirst && b->rules == RULES_CHINESE);
	bool can_pass_first = (!pachi_nopassfirst || pass_all_alive);
	if (!can_pass_first)  return false;

	if (is_pass(coord) || is_pass(last_move(b).coord))  return false;

	enum stone other_color = stone_other(color);
	if (board_playing_ko_threat(b))  return false;

	/* Find dames left */
	move_queue_t dead, unclear;
	uct_mcowner_playouts(u, b, color);
	ownermap_dead_groups(b, &u->ownermap, &dead, &unclear);
	if (unclear.moves)  return false;
	int final_ownermap[board_max_coords(b)];
	int dame, seki;
	board_official_score_details(b, &dead, &dame, &seki, final_ownermap, &u->ownermap);
	
	enum stone move_owner = ownermap_color(&u->ownermap, coord, 0.80);
	return (!dame && move_owner == other_color); /* play in opponent territory */
}

tree_node_t *
uct_search_result(uct_t *u, board_t *b, enum stone color,
		  bool pass_all_alive, int played_games, int base_playouts,
		  coord_t *best_coord)
{
	/* Choose the best move from the tree. */
	tree_node_t *best = u->policy->choose(u->policy, u->t->root, b, color, resign);
	if (!best) {
		*best_coord = pass;
		return NULL;
	}
	*best_coord = node_coord(best);
	floating_t winrate = tree_node_get_value(u->t, 1, best->u.value);

	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %s with score %1.4f (%d/%d:%d/%d games), extra komi %f\n",
			coord2sstr(node_coord(best)), winrate,
			best->u.playouts, u->t->root->u.playouts,
			u->t->root->u.playouts - base_playouts, played_games,
			u->t->extra_komi);

	/* Do not resign if we're so short of time that evaluation of best
	 * move is completely unreliable, we might be winning actually.
	 * In this case best is almost random but still better than resign. */
	if (winrate < u->resign_threshold && !is_pass(node_coord(best))
	    // If only simulated node has been a pass and no other node has
	    // been simulated but pass won't win, an unsimulated node has
	    // been returned; test therefore also for #simulations at root.
	    && (best->u.playouts > GJ_MINGAMES || u->t->root->u.playouts > GJ_MINGAMES * 2)
	    && !u->t->untrustworthy_tree) {
		if (UDEBUGL(0)) fprintf(stderr, "<resign>\n");
		*best_coord = resign;
		return NULL;
	}

	char *msg;
	
	/* Pass best move ? Still check if it's safe to do so
	 * so we get (hopefully) good dead groups for scoring phase. */
	if (is_pass(*best_coord)) {
		if (uct_search_pass_is_safe(u, b, color, pass_all_alive, &msg)) {
			if (UDEBUGL(0)) fprintf(stderr, "<Looks safe enough. Final score: %s>\n", board_official_score_str(b, &u->dead_groups));
			return best;
		}
		if (UDEBUGL(1))	fprintf(stderr, "Pass looks unsafe, we might be screwed (%s)\n", msg);
		return best;
	}
	
	bool opponent_passed = is_pass(last_move(b).coord);
	bool pass_first = uct_pass_first(u, b, color, pass_all_alive, *best_coord);
	if (UDEBUGL(2) && pass_first)  fprintf(stderr, "pass first ok\n");

	/* If the opponent just passed and we win counting, always pass as well.
	 * Pass also instead of playing in opponent territory if winning.
	 * For option stones_only, we pass only when there is nothing else to do,
	 * to show how to maximize score. */
	if ((opponent_passed || pass_first) &&
	    b->moves > 10 && b->rules != RULES_STONES_ONLY) {
		if (uct_search_pass_is_safe(u, b, color, pass_all_alive, &msg)) {
			if (UDEBUGL(0))  fprintf(stderr, "<Will rather pass, looks safe enough. Final score: %s>\n", board_official_score_str(b, &u->dead_groups));
			*best_coord = pass;
			return NULL;
		}
		if (UDEBUGL(2))	fprintf(stderr, "Refusing to pass: %s\n", msg);
	}
	
	return best;
}
