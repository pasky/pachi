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
#include "pachi.h"
#include "distributed/distributed.h"
#include "move.h"
#include "joseki.h"
#include "random.h"
#include "timeinfo.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"
#include "uct/prior.h"
#include "dcnn.h"


/* Default time settings for the UCT engine. In distributed mode, slaves are
 * unlimited by default and all control is done on the master, either in time
 * or with total number of playouts over all slaves. (It is also possible but
 * not recommended to limit only the slaves; the master then decides the move
 * when a majority of slaves have made their choice.) */
static struct time_info default_ti;
static __attribute__((constructor)) void
default_ti_init(void)
{
	time_parse(&default_ti, "10");
}

static const struct time_info unlimited_ti = {
	.period = TT_MOVE,
	.dim = TD_GAMES,
	.len = { .games = INT_MAX, .games_max = 0 },
};

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
 * | spawn_thread_manager()
 * | -----------------------
 * | spawn_worker()
 * V uct_playouts() */

/* Set in thread manager in case the workers should stop. */
volatile sig_atomic_t uct_halt = 0;
/* ID of the thread manager. */
static pthread_t thread_manager;
bool thread_manager_running;

static pthread_mutex_t finish_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;
static volatile int finish_thread;
static pthread_mutex_t finish_serializer = PTHREAD_MUTEX_INITIALIZER;

static void  uct_expand_next_best_moves(struct uct *u, struct tree *t, struct board *b, enum stone color);
static void *spawn_logger(void *ctx_);

static void *
spawn_worker(void *ctx_)
{
	/* Setup */
	struct uct_thread_ctx *ctx = ctx_;
	struct uct *u = ctx->u;
	fast_srandom(ctx->seed);

	/* Fill ownermap for mcowner pattern feature. */
	if (using_patterns()) {
		double time_start = time_now();
		uct_mcowner_playouts(ctx->u, ctx->b, ctx->color);	
		if (!ctx->tid) {
			if (DEBUGL(2))  fprintf(stderr, "mcowner %.2fs\n", time_now() - time_start);
			//fprintf(stderr, "\npattern ownermap:\n");
			//board_print_ownermap(ctx->b, stderr, &u->ownermap);
		}
	}

	/* Expand root node (dcnn). Other threads wait till it's ready. 
	 * For dcnn pondering we also need dcnn values for opponent's best moves. */
	struct tree *t = ctx->t;
	struct tree_node *n = t->root;
	if (!ctx->tid) {
		enum stone player_color = ctx->color;
		enum stone node_color = stone_other(player_color);
		assert(node_color == t->root_color);
		
		if (tree_leaf_node(n) && !__sync_lock_test_and_set(&n->is_expanded, 1)) {
			tree_expand_node(t, n, ctx->b, player_color, u, 1);
			if (u->genmove_pondering && using_dcnn(ctx->b))
				uct_expand_next_best_moves(u, t, ctx->b, player_color);
		}
		else if (DEBUGL(2)) {  /* Show previously computed priors */
			print_joseki_moves(joseki_dict, ctx->b, ctx->color);
			print_node_prior_best_moves(ctx->b, n);
		}
		u->tree_ready = true;
	}
	else while (!u->tree_ready)
		     usleep(100 * 1000);

	/* Run */
	if (!ctx->tid)  u->mcts_time_start = time_now();
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
spawn_thread_manager(void *ctx_)
{
	/* In thread_manager, we use only some of the ctx fields. */
	struct uct_thread_ctx *mctx = ctx_;
	struct uct *u = mctx->u;
	struct tree *t = mctx->t;
	fast_srandom(mctx->seed);

	int played_games = 0;
	pthread_t threads[u->threads + 1];
	int joined = 0;

	uct_halt = 0;

	/* Garbage collect the tree by preference when pondering. */
	if (u->pondering && t->nodes && t->nodes_size >= t->pruning_threshold) {
		t->root = tree_garbage_collect(t, t->root);
	}

	u->tree_ready = false;
		
	/* Logging thread for pondering */
	if (u->pondering)
		pthread_create(&threads[u->threads], NULL, spawn_logger, mctx);
	
	/* Spawn threads... */
	for (int ti = 0; ti < u->threads; ti++) {
		struct uct_thread_ctx *ctx = malloc2(sizeof(*ctx));
		ctx->u = u; ctx->b = mctx->b; ctx->color = mctx->color;
		mctx->t = ctx->t = t;
		ctx->tid = ti; ctx->seed = fast_random(65536) + ti;
		ctx->ti = mctx->ti;
		pthread_attr_t a;
		pthread_attr_init(&a);
		pthread_attr_setstacksize(&a, 1048576);
		pthread_create(&threads[ti], &a, spawn_worker, ctx);
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
		struct uct_thread_ctx *ctx;
		pthread_join(threads[finish_thread], (void **) &ctx);
		played_games += ctx->games;
		joined++;
		free(ctx);
		if (UDEBUGL(4))
			fprintf(stderr, "Joined worker %d\n", finish_thread);
		pthread_mutex_unlock(&finish_serializer);
	}

	if (u->pondering)
		pthread_join(threads[u->threads], NULL);
	
	pthread_mutex_unlock(&finish_mutex);

	mctx->games = played_games;
	return mctx;
}

/* Pondering: Logging thread */
static void *
spawn_logger(void *ctx_)
{
	struct uct_thread_ctx *ctx = ctx_;
	struct uct *u = ctx->u;
	struct tree *t = ctx->t;
	struct board *b = ctx->b;
	enum stone color = ctx->color;
	struct uct_search_state *s = ctx->s;
	struct time_info *ti = ctx->ti;

	// Similar to uct_search() code when pondering
	while (!uct_halt) {
		time_sleep(TREE_BUSYWAIT_INTERVAL);
		/* TREE_BUSYWAIT_INTERVAL should never be less than desired time, or the
		 * time control is broken. But if it happens to be less, we still search
		 * at least 100ms otherwise the move is completely random. */

		int i = uct_search_games(s);
		/* Print notifications etc. */
		uct_search_progress(u, b, color, t, ti, s, i);
		
		if (s->fullmem)  uct_pondering_stop(u);
	}
	return NULL;
}

/* Expand next move node (dcnn pondering) */
static void
uct_expand_next_move(struct uct *u, struct tree *t, struct board *board, enum stone color, coord_t c)
{
	struct tree_node *n = tree_get_node(t->root, c);
	assert(n && tree_leaf_node(n) && !n->is_expanded);
	
	struct board b;
	board_copy(&b, board);

	struct move m = { .coord = c, .color = color };
	int res = board_play(&b, &m);
	if (res < 0) goto done;
		
	if (!__sync_lock_test_and_set(&n->is_expanded, 1))
		tree_expand_node(t, n, &b, stone_other(color), u, -1);

 done:  board_done_noalloc(&b);
}

/* For pondering with dcnn we need dcnn values for next move as well before
 * search starts. Can't evaluate all of them, so guess from prior best moves +
 * genmove's best moves for opponent. If we guess right all is well. If we
 * guess wrong pondering will not be useful for this move, search results
 * will be discarded. */
static void
uct_expand_next_best_moves(struct uct *u, struct tree *t, struct board *b, enum stone color)
{
	assert(using_dcnn(b));
	struct move_queue q = { .moves = 0 };
	
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
			fprintf(stderr, "%s ", coord2sstr(q.move[i], b));
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
	s->print_interval = u->reportfreq;
	s->fullmem = false;

	if (ti) {
		if (ti->period == TT_NULL) {
			if (u->slave)
				*ti = unlimited_ti;
			else {
				*ti = default_ti;
				time_start_timer(ti);
			}
		}
		time_stop_conditions(ti, b, u->fuseki_end, u->yose_start, u->max_maintime_ratio, &s->stop);
	}

	/* Fire up the tree search thread manager, which will in turn
	 * spawn the searching threads. */
	assert(u->threads > 0);
	assert(!thread_manager_running);
	static struct uct_thread_ctx mctx;
	mctx = (struct uct_thread_ctx) { .u = u, .b = b, .color = color, .t = t, .seed = fast_random(65536), .ti = ti, .s = s };
	s->ctx = &mctx;
	pthread_mutex_lock(&finish_serializer);
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
	int di = u->dynkomi_interval * u->threads;
	if (ctx->t->use_extra_komi && u->dynkomi->permove
	    && !u->pondering && di
	    && i > s->last_dynkomi + di) {
		s->last_dynkomi += di;
		floating_t old_dynkomi = ctx->t->extra_komi;
		ctx->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, ctx->t);
		if (UDEBUGL(3) && old_dynkomi != ctx->t->extra_komi)
			fprintf(stderr, "dynkomi adjusted (%f -> %f)\n",
				old_dynkomi, ctx->t->extra_komi);
	}

	/* Print progress? */
	if (i - s->last_print > s->print_interval) {
		s->last_print += s->print_interval; // keep the numbers tidy
		uct_progress_status(u, ctx->t, color, s->last_print, NULL);
	}

	if (!s->fullmem && ctx->t->nodes_size > u->max_tree_size) {
		char *msg = "WARNING: Tree memory limit reached, stopping search.\n"
			    "Try increasing max_tree_size.\n";
		if (UDEBUGL(2))  fprintf(stderr, "%s", msg);
#ifdef _WIN32
		popup(msg);
#endif
		s->fullmem = true;
	}
}


/* Determine whether we should terminate the search early. */
static bool
uct_search_stop_early(struct uct *u, struct tree *t, struct board *b,
		struct time_info *ti, struct time_stop *stop,
		struct tree_node *best, struct tree_node *best2,
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
		elapsed = time_now() - ti->len.t.timer_start;
		if (elapsed < TREE_BUSYWAIT_INTERVAL) return false;
	}

	/* Break early if we estimate the second-best move cannot
	 * catch up in assigned time anymore. We use all our time
	 * if we are in byoyomi with single stone remaining in our
	 * period, however - it's better to pre-ponder. */
	bool time_indulgent = (!ti->len.t.main_time && ti->len.t.byoyomi_stones == 1);
	if (best2 && ti->dim == TD_WALLTIME
	    && played >= PLAYOUT_EARLY_BREAK_MIN && !time_indulgent) {
		double remaining = stop->worst.time - elapsed;
		double pps = ((double)played) / elapsed;
		double estplayouts = remaining * pps + PLAYOUT_DELTA_SAFEMARGIN;
		if (best->u.playouts > best2->u.playouts + estplayouts) {
			if (UDEBUGL(2))
				fprintf(stderr, "Early stop, result cannot change: "
					"best %d, best2 %d, estimated %f simulations to go (%d/%f=%f pps)\n",
					best->u.playouts, best2->u.playouts, estplayouts, played, elapsed, pps);
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
	floating_t beta = 2 * (tree_node_get_value(t, 1, best->u.value) - 0.5);
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
				coord2sstr(node_coord(best), t->board),
				best->u.playouts, tree_node_get_value(t, 1, best->u.value),
				coord2sstr(node_coord(winner), t->board),
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
	assert(!(ti->dim == TD_GAMES && ti->len.games < GJ_MINGAMES));
	if (i < GJ_MINGAMES)
		return false;

	struct tree_node *best = NULL;
	struct tree_node *best2 = NULL; // Second-best move.
	struct tree_node *bestr = NULL; // best's best child.
	struct tree_node *winner = NULL;

	best = u->policy->choose(u->policy, ctx->t->root, b, color, resign);
	if (best) best2 = u->policy->choose(u->policy, ctx->t->root, b, color, node_coord(best));

	/* Possibly stop search early if it's no use to try on. */
	int played = u->played_all + i - s->base_playouts;
	if (best && uct_search_stop_early(u, ctx->t, b, ti, &s->stop, best, best2, played, s->fullmem))
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
	 * and we already have enough playouts (possibly thanks to tbook
	 * or to pondering)? */
	return false;
}

/* uct_pass_is_safe() also called by uct policy, beware.  */
static bool
uct_search_pass_is_safe(struct uct *u, struct board *b, enum stone color, bool pass_all_alive, char **msg)
{
	bool res = uct_pass_is_safe(u, b, color, pass_all_alive, msg);

	/* Save dead groups for final_status_list dead. */
	if (res) {
		struct move_queue unclear;
		struct move_queue *dead = &u->dead_groups;
		u->pass_moveno = b->moves + 1;
		get_dead_groups(b, &u->ownermap, dead, &unclear);
	}
	return res;
}

struct tree_node *
uct_search_result(struct uct *u, struct board *b, enum stone color,
		  bool pass_all_alive, int played_games, int base_playouts,
		  coord_t *best_coord)
{
	/* Choose the best move from the tree. */
	enum stone other_color = stone_other(color);
	struct tree_node *best = u->policy->choose(u->policy, u->t->root, b, color, resign);
	if (!best) {
		*best_coord = pass;
		return NULL;
	}
	*best_coord = node_coord(best);
	floating_t winrate = tree_node_get_value(u->t, 1, best->u.value);

	if (UDEBUGL(1))
		fprintf(stderr, "*** WINNER is %s with score %1.4f (%d/%d:%d/%d games), extra komi %f\n",
			coord2sstr(node_coord(best), b), winrate,
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

	bool opponent_passed = is_pass(b->last_move.coord);
	bool pass_first = false;
	if (!is_pass(*best_coord)) {
		enum stone move_owner = ownermap_color(&u->ownermap, *best_coord, 0.80);
		int capturing = board_get_atari_neighbor(b, *best_coord, other_color);
		floating_t score = ownermap_score_est_color(b, &u->ownermap, color);
		bool can_pass_first = (!nopassfirst || pass_all_alive);  /* For kgs: must not pass first in main game phase. */
		pass_first = (can_pass_first && (move_owner == other_color) && /* play in opponent territory */
			      !capturing && !board_playing_ko_threat(b) &&
			      winrate > 0.80 && score > 1.0);
	}

	/* If the opponent just passed and we win counting, always pass as well.
	 * Pass also instead of playing in opponent territory if winning.
	 * For option stones_only, we pass only when there is nothing else to do,
	 * to show how to maximize score. */
	if ((opponent_passed || pass_first) &&
	    b->moves > 10 && b->rules != RULES_STONES_ONLY) {
		char *msg;
		if (uct_search_pass_is_safe(u, b, color, pass_all_alive, &msg)) {
			if (UDEBUGL(0)) {
				float score = -1 * board_official_score(b, &u->dead_groups);
				fprintf(stderr, "<Will rather pass, looks safe enough. Final score: %s%.1f>\n",
					(score > 0 ? "B+" : "W+"), fabs(score));
			}
			*best_coord = pass;
			return NULL;
		}
		if (UDEBUGL(0))	fprintf(stderr, "Refusing to pass: %s\n", msg);
	}
	
	return best;
}
