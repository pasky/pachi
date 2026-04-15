#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "ownermap.h"
#include "playout.h"
#include "playout/moggy.h"
#include "pattern/mcowner.h"


/******************************************************************************************/
/* Thread management */

/* Batch playouts thread context */
typedef struct {
	int tid;
	int games;
	uint64_t seed;
	board_t *b;
	enum stone color;
	playout_t *playout;
	ownermap_t *ownermap;
	bool amafmap_needed;
	collect_data_t collect_data;
	void *data;
} mcowner_thread_ctx_t;

static int thread_playouts;

static void *
mcowner_worker_thread(void *ctx_)
{
	/* Setup */
	mcowner_thread_ctx_t *ctx = (mcowner_thread_ctx_t*)ctx_;
	uint64_t random_state;
	fast_srandom(&random_state, ctx->seed);

	/* Run */
	while (thread_playouts < ctx->games) {
		batch_playout(ctx->b, ctx->color, ctx->playout, ctx->ownermap, ctx->amafmap_needed, ctx->collect_data, ctx->data);
		__sync_fetch_and_add(&thread_playouts, 1);
	}

	return NULL;
}

static void
mcowner_spawn_threads(int threads, int games, board_t *b, enum stone color,
		      playout_t *playout, ownermap_t *ownermap, bool amafmap_needed,
		      collect_data_t collect_data, void *data)
{
	assert(threads > 0);
	
	thread_playouts = 0;
	
	/* Spawn threads... */
	pthread_t pthreads[threads];
	mcowner_thread_ctx_t threads_ctx[threads];
	for (int ti = 0; ti < threads; ti++) {
		mcowner_thread_ctx_t *ctx = &threads_ctx[ti];
		ctx->tid = ti;
		ctx->games = games;
		ctx->seed = fast_random(65536) + ti;
		ctx->b = b;
		ctx->color = color;
		ctx->playout = playout;
		ctx->ownermap = ownermap;
		ctx->amafmap_needed = amafmap_needed;
		ctx->collect_data = collect_data;
		ctx->data = data;
		
		pthread_attr_t a;
		pthread_attr_init(&a);
		pthread_attr_setstacksize(&a, 1048576);
		pthread_create(&pthreads[ti], &a, mcowner_worker_thread, ctx);
		if (DEBUGL(4))  fprintf(stderr, "Spawned worker %d\n", ti);
	}

	/* ...and collect them back: */
	for (int ti = 0; ti < threads; ti++) {
		pthread_join(pthreads[ti], NULL);
		if (DEBUGL(4))  fprintf(stderr, "Joined worker %d\n", ti);
	}
}


/******************************************************************************************/
/* MCowner playouts */

/* Play one game and record (optional) ownermap. */
int
batch_playout(board_t *board, enum stone color, playout_t *playout,
	      ownermap_t *ownermap, bool amafmap_needed,
	      collect_data_t collect_data, void *data)
{
	board_t b2;  board_copy(&b2, board);
	board_t *b = &b2;

	/* amafmap: if needed each worker must have its own. */
	amafmap_t local_amafmap;
	amafmap_t *amafmap = NULL;
	if (amafmap_needed) {
		amafmap = &local_amafmap;
		amaf_init(amafmap);
	}

	/* Get score from black's perspective */
	floating_t score = playout_play_game(playout, b, color, amafmap, ownermap);
	score = -score;

	if (collect_data)
		collect_data(board, color, b, score, amafmap, data);

	board_done(b);
	return score;
}

void
batch_playouts(int threads, int games, board_t *b, enum stone color,
	       ownermap_t *ownermap, bool amafmap_needed,
	       collect_data_t collect_data, void *data)
{
	static playout_policy_t *policy = NULL;
	if (!policy)   policy = playout_moggy_init(NULL, b);
	
	playout_setup_t setup = playout_setup(MAX_GAMELEN, 0);
	playout_t playout = { &setup, policy };

	if (ownermap)  ownermap_init(ownermap);

	assert(threads > 0);
	if (threads == 1)
		for (int i = 0; i < games; i++)
			batch_playout(b, color, &playout, ownermap, amafmap_needed, collect_data, data);
	else
		mcowner_spawn_threads(threads, games, b, color, &playout, ownermap, amafmap_needed, collect_data, data);

	//fprintf(stderr, "pattern ownermap:\n");
	//board_print_ownermap(b, stderr, ownermap);
}

void
mcowner_playouts(int threads, int games, board_t *b, enum stone color, ownermap_t *ownermap)
{
	batch_playouts(threads, games, b, color, ownermap, false, NULL, NULL);
}

void
mcowner_playouts_fast(board_t *b, enum stone color, ownermap_t *ownermap)
{
	batch_playouts(MAX_THREADS, 100, b, color, ownermap, false, NULL, NULL);
}
