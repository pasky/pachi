#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "gtp.h"
#include "chat.h"
#include "move.h"
#include "mq.h"
#include "joseki.h"
#include "playout.h"
#include "playout/moggy.h"
#include "playout/light.h"
#include "tactics/util.h"
#include "timeinfo.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/plugins.h"
#include "uct/prior.h"
#include "uct/search.h"
#include "uct/slave.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"
#include "dcnn.h"

uct_policy_t *policy_ucb1_init(uct_t *u, char *arg);
uct_policy_t *policy_ucb1amaf_init(uct_t *u, char *arg, board_t *board);
static void uct_pondering_start(uct_t *u, board_t *b0, tree_t *t, enum stone color, coord_t our_move, bool genmove_pondering);

/* Maximal simulation length. */
#define MC_GAMELEN	MAX_GAMELEN

static void
setup_state(uct_t *u, board_t *b, enum stone color)
{
	u->t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0,
			 u->max_pruned_size, u->pruning_threshold, u->local_tree_aging, u->stats_hbits);
	if (u->initial_extra_komi)
		u->t->extra_komi = u->initial_extra_komi;
	if (u->force_seed)
		fast_srandom(u->force_seed);
	if (UDEBUGL(3))
		fprintf(stderr, "Fresh board with random seed %lu\n", fast_getseed());
	if (!u->no_tbook && b->moves == 0) {
		if (color == S_BLACK) {
			tree_load(u->t, b);
		} else if (DEBUGL(0)) {
			fprintf(stderr, "Warning: First move appears to be white\n");
		}
	}
}

static void
reset_state(uct_t *u)
{
	assert(u->t);
	tree_done(u->t); u->t = NULL;
}

static void
setup_dynkomi(uct_t *u, board_t *b, enum stone to_play)
{
	if (u->t->use_extra_komi && !u->pondering && u->dynkomi->permove)
		u->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, u->t);
	else if (!u->t->use_extra_komi)
		u->t->extra_komi = 0;
}

void
uct_prepare_move(uct_t *u, board_t *b, enum stone color)
{
	if (u->t) {
		/* Verify that we have sane state. */
		assert(b->es == u);
		assert(u->t && b->moves);
		assert(node_coord(u->t->root) == b->last_move.coord);
		assert(u->t->root_color == b->last_move.color);
		if (color != stone_other(u->t->root_color))
			die("Fatal: Non-alternating play detected %d %d\n", color, u->t->root_color);
		uct_htable_reset(u->t);
	} else {
		/* We need fresh state. */
		b->es = u;
		setup_state(u, b, color);
	}

	ownermap_init(&u->ownermap);
	u->played_own = u->played_all = 0;
}

/* Does the board look like a final position ?
 * And do we win counting, considering that given groups are dead ?
 * (if allow_losing_pass wasn't set) */
bool
uct_pass_is_safe(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, char **msg)
{
	/* Check this early, no need to go through the whole thing otherwise. */
	*msg = "too early to pass";
	if (b->moves < board_earliest_pass(b))
		return false;
	
	/* Make sure enough playouts are simulated to get a reasonable dead group list. */
	move_queue_t dead, unclear;	
	uct_mcowner_playouts(u, b, color);
	get_dead_groups(b, &u->ownermap, &dead, &unclear);

	bool check_score = !u->allow_losing_pass;

	if (pass_all_alive) {
		*msg = "need to remove opponent dead groups first";
		for (unsigned int i = 0; i < dead.moves; i++)
			if (board_at(b, dead.move[i]) == stone_other(color))
				return false;
		dead.moves = 0; // our dead stones are alive when pass_all_alive is true

		float final_score = board_official_score_color(b, &dead, color);
		*msg = "losing on official score";
		return (check_score ? final_score >= 0 : true);
	}

	/* Check score estimate first, official score is off if position is not final */
	*msg = "losing on score estimate";
	floating_t score_est = ownermap_score_est_color(b, &u->ownermap, color);
	if (check_score && score_est < 0)  return false;
	
	int final_ownermap[board_size2(b)];
	int dame, seki;
	floating_t final_score = board_official_score_details(b, &dead, &dame, &seki, final_ownermap, &u->ownermap);
	if (color == S_BLACK)  final_score = -final_score;
	
	/* Don't go to counting if position is not final. */
	if (!board_position_final_full(b, &u->ownermap, &dead, &unclear, score_est,
				       final_ownermap, dame, final_score, msg))
		return false;
	
	*msg = "losing on official score";
	return (check_score ? final_score >= 0 : true);
}

static void
uct_board_print(engine_t *e, board_t *b, FILE *f)
{
	uct_t *u = b->es;
	board_print_ownermap(b, f, (u ? &u->ownermap : NULL));
}

/* Fill ownermap for mcowner pattern feature (no tree search)
 * ownermap must be initialized already. */
void
uct_mcowner_playouts(uct_t *u, board_t *b, enum stone color)
{
	playout_setup_t ps = playout_setup(u->gamelen, u->mercymin);
	
	/* TODO pick random last move, better playouts randomness */

	while (u->ownermap.playouts < GJ_MINGAMES) {
		board_t b2;
		board_copy(&b2, b);
		playout_play_game(&ps, &b2, color, NULL, &u->ownermap, u->playout);
		board_done_noalloc(&b2);
	}
}

static ownermap_t*
uct_ownermap(engine_t *e, board_t *b)
{
	uct_t *u = b->es;
	
	/* Make sure ownermap is well-seeded. */
	enum stone color = (b->last_move.color ? stone_other(b->last_move.color) : S_BLACK);
	uct_mcowner_playouts(u, b, color);
	
	return &u->ownermap;
}

static char *
uct_notify_play(engine_t *e, board_t *b, move_t *m, char *enginearg)
{
	uct_t *u = e->data;
	if (!u->t) {
		/* No state, create one - this is probably game beginning
		 * and we need to load the opening tbook right now. */
		uct_prepare_move(u, b, m->color);
		assert(u->t);
	}

	/* Stop pondering, required by tree_promote_at() */
	uct_pondering_stop(u);
	if (UDEBUGL(2) && u->slave)  tree_dump(u->t, u->dumpthres);

	if (is_resign(m->coord)) {
		/* Reset state. */
		reset_state(u);
		return NULL;
	}

	/* Promote node of the appropriate move to the tree root.
	 * If using dcnn, only promote node if it has dcnn priors:
	 * Direction of tree search is heavily influenced by initial priors,
	 * if we started searching without dcnn data better start from scratch. */
	int reason;	
	assert(u->t->root);
	if (u->t->untrustworthy_tree || !tree_promote_at(u->t, b, m->coord, &reason)) {
		if (UDEBUGL(3)) {
			if      (u->t->untrustworthy_tree)  fprintf(stderr, "Not promoting move node in untrustworthy tree.\n");
			else if (reason == TREE_HINT_DCNN)  fprintf(stderr, "Played move has no dcnn priors, resetting tree.\n");
			else				    fprintf(stderr, "Warning: Cannot promote move node! Several play commands in row?\n");
		}

		/* Preserve dynamic komi information, though, that is important. */
		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
		return NULL;
	}

	/* If we are a slave in a distributed engine, start pondering once
	 * we know which move we actually played. See uct_genmove() about
	 * the check for pass. */
	if (u->pondering_opt && u->slave && m->color == u->my_color && !is_pass(m->coord))
		uct_pondering_start(u, b, u->t, stone_other(m->color), m->coord, false);
	assert(!(u->slave && using_dcnn(b))); // XXX distributed engine dcnn pondering support

	return NULL;
}

static char *
uct_result(engine_t *e, board_t *b)
{
	uct_t *u = e->data;
	static char reply[1024];

	if (!u->t)
		return NULL;
	enum stone color = u->t->root_color;
	tree_node_t *n = u->t->root;
	snprintf(reply, 1024, "%s %s %d %.2f %.1f",
		 stone2str(color), coord2sstr(node_coord(n)),
		 n->u.playouts, tree_node_get_value(u->t, -1, n->u.value),
		 u->t->use_extra_komi ? u->t->extra_komi : 0);
	return reply;
}

static char *
uct_chat(engine_t *e, board_t *b, bool opponent, char *from, char *cmd)
{
	uct_t *u = e->data;

	if (!u->t)
		return generic_chat(b, opponent, from, cmd, S_NONE, pass, 0, 1, u->threads, 0.0, 0.0, "");

	tree_node_t *n = u->t->root;
	double winrate = tree_node_get_value(u->t, -1, n->u.value);
	double extra_komi = u->t->use_extra_komi && fabs(u->t->extra_komi) >= 0.5 ? u->t->extra_komi : 0;
	char *score_est = ownermap_score_est_str(b, &u->ownermap);

	return generic_chat(b, opponent, from, cmd, u->t->root_color, node_coord(n), n->u.playouts, 1,
			    u->threads, winrate, extra_komi, score_est);
}

static void
print_dead_groups(uct_t *u, board_t *b, move_queue_t *dead)
{
	fprintf(stderr, "dead groups (playing %s)\n", (u->my_color ? stone2str(u->my_color) : "???"));
	if (!dead->moves)
		fprintf(stderr, "  none\n");
	for (unsigned int i = 0; i < dead->moves; i++) {
		fprintf(stderr, "  ");
		foreach_in_group(b, dead->move[i]) {
			fprintf(stderr, "%s ", coord2sstr(c));
		} foreach_in_group_end;
		fprintf(stderr, "\n");
	}
}


static void
uct_dead_group_list(engine_t *e, board_t *b, move_queue_t *dead)
{
	uct_t *u = e->data;
	
	/* This means the game is probably over, no use pondering on. */
	uct_pondering_stop(u);
	
	if (u->pass_all_alive)
		return; // no dead groups

	/* Normally last genmove was a pass and we've already figured out dead groups.
	 * Don't recompute dead groups here, result could be different this time and
	 * lead to wrong list. */
	if (u->pass_moveno == b->moves || u->pass_moveno == b->moves - 1) {
		memcpy(dead, &u->dead_groups, sizeof(*dead));
		print_dead_groups(u, b, dead);
		return;
	}

	fprintf(stderr, "WARNING: Recomputing dead groups\n");

	/* Make sure the ownermap is well-seeded. */
	uct_mcowner_playouts(u, b, S_BLACK);
	if (DEBUGL(2))  board_print_ownermap(b, stderr, &u->ownermap);

	get_dead_groups(b, &u->ownermap, dead, NULL);
	print_dead_groups(u, b, dead);
}

static void
uct_stop(engine_t *e)
{
	/* This is called on game over notification. However, an undo
	 * and game resume can follow, so don't panic yet and just
	 * relax and stop thinking so that we don't waste CPU. */
	uct_t *u = e->data;
	uct_pondering_stop(u);
}

/* This is called on engine reset, especially when clear_board
 * is received and new game should begin. */
static void
uct_done(engine_t *e)
{
	uct_t *u = e->data;

	free(u->banner);
	uct_pondering_stop(u);
	if (u->t)             reset_state(u);
	if (u->dynkomi)       u->dynkomi->done(u->dynkomi);
	if (u->policy)        u->policy->done(u->policy);
	if (u->random_policy) u->random_policy->done(u->random_policy);
	playout_policy_done(u->playout);
	uct_prior_done(u->prior);
#ifdef PACHI_PLUGINS
	pluginset_done(u->plugins);
#endif
}



/* Run time-limited MCTS search on foreground. */
static int
uct_search(uct_t *u, board_t *b, time_info_t *ti, enum stone color, tree_t *t, bool print_progress)
{
	uct_search_state_t s;
	uct_search_start(u, b, color, t, ti, &s);
	if (UDEBUGL(2) && s.base_playouts > 0)
		fprintf(stderr, "<pre-simulated %d games>\n", s.base_playouts);

	/* The search tree is ctx->t. This is currently == . It is important
	 * to reference ctx->t directly since the
	 * thread manager will swap the tree pointer asynchronously. */

	/* Now, just periodically poll the search tree. */
	/* Note that in case of TD_GAMES, threads will not wait for
	 * the uct_search_check_stop() signalization. */
	while (1) {
		time_sleep(TREE_BUSYWAIT_INTERVAL);
		/* TREE_BUSYWAIT_INTERVAL should never be less than desired time, or the
		 * time control is broken. But if it happens to be less, we still search
		 * at least 100ms otherwise the move is completely random. */

		int i = uct_search_games(&s);
		/* Print notifications etc. */
		uct_search_progress(u, b, color, t, ti, &s, i);
		/* Check if we should stop the search. */
		if (uct_search_check_stop(u, b, color, t, ti, &s, i))
			break;
	}

	uct_thread_ctx_t *ctx = uct_search_stop();
	if (UDEBUGL(3)) tree_dump(t, u->dumpthres);
	if (UDEBUGL(2))
		fprintf(stderr, "(avg score %f/%d; dynkomi's %f/%d value %f/%d)\n",
			t->avg_score.value, t->avg_score.playouts,
			u->dynkomi->score.value, u->dynkomi->score.playouts,
			u->dynkomi->value.value, u->dynkomi->value.playouts);
	if (print_progress)
		uct_progress_status(u, t, color, ctx->games, NULL);

	if (u->debug_after.playouts > 0) {
		/* Now, start an additional run of playouts, single threaded. */
		time_info_t debug_ti;
		debug_ti.period = TT_MOVE;
		debug_ti.dim = TD_GAMES;
		debug_ti.len.games = t->root->u.playouts + u->debug_after.playouts;
		debug_ti.len.games_max = 0;

		board_print_ownermap(b, stderr, &u->ownermap);
		fprintf(stderr, "--8<-- UCT debug post-run begin (%d:%d) --8<--\n", u->debug_after.level, u->debug_after.playouts);

		int debug_level_save = debug_level;
		int u_debug_level_save = u->debug_level;
		int p_debug_level_save = u->playout->debug_level;
		debug_level = u->debug_after.level;
		u->debug_level = u->debug_after.level;
		u->playout->debug_level = u->debug_after.level;
		uct_halt = false;

		uct_playouts(u, b, color, t, &debug_ti);
		tree_dump(t, u->dumpthres);

		uct_halt = true;
		debug_level = debug_level_save;
		u->debug_level = u_debug_level_save;
		u->playout->debug_level = p_debug_level_save;

		fprintf(stderr, "--8<-- UCT debug post-run finished --8<--\n");
	}

	u->played_own += ctx->games;
	return ctx->games;
}

/* Start pondering background with @color to play.
 * @our_move: move to be added before starting. 0 means doesn't apply. */
static void
uct_pondering_start(uct_t *u, board_t *b0, tree_t *t, enum stone color, coord_t our_move, bool genmove_pondering)
{
	if (UDEBUGL(1))
		fprintf(stderr, "Starting to ponder with color %s\n", stone2str(stone_other(color)));
	u->pondering = true;
	u->genmove_pondering = genmove_pondering;

	/* We need a local board copy to ponder upon. */
	board_t *b = malloc2(sizeof(*b)); board_copy(b, b0);

	/* Board needs updating ? (b0 did not have the genmove'd move played yet) */
	if (our_move) {	          /* 0 never a real coord */
		move_t m = move(our_move, stone_other(color));
		int res = board_play(b, &m);
		assert(res >= 0);
	}
	if (b->last_move.color != S_NONE)
		assert(b->last_move.color == stone_other(color));
	
	setup_dynkomi(u, b, color);

	/* Start MCTS manager thread "headless". */
	static uct_search_state_t s;
	uct_search_start(u, b, color, t, NULL, &s);
}

/* uct_search_stop() frontend for the pondering (non-genmove) mode, and
 * to stop the background search for a slave in the distributed engine. */
void
uct_pondering_stop(uct_t *u)
{
	if (!thread_manager_running)
		return;

	/* Stop the thread manager. */
	uct_thread_ctx_t *ctx = uct_search_stop();
	if (UDEBUGL(1)) {
		if (u->pondering) fprintf(stderr, "(pondering) ");
		uct_progress_status(u, ctx->t, ctx->color, ctx->games, NULL);
	}
	if (u->pondering) {
		free(ctx->b);
		u->pondering = false;
	}
}


void
uct_genmove_setup(uct_t *u, board_t *b, enum stone color)
{
	if (b->superko_violation) {
		fprintf(stderr, "!!! WARNING: SUPERKO VIOLATION OCCURED BEFORE THIS MOVE\n");
		fprintf(stderr, "Maybe you play with situational instead of positional superko?\n");
		fprintf(stderr, "I'm going to ignore the violation, but note that I may miss\n");
		fprintf(stderr, "some moves valid under this ruleset because of this.\n");
		b->superko_violation = false;
	}

	uct_prepare_move(u, b, color);

	assert(u->t);
	u->my_color = color;

	/* How to decide whether to use dynkomi in this game? Since we use
	 * pondering, it's not simple "who-to-play" matter. Decide based on
	 * the last genmove issued. */
	u->t->use_extra_komi = !!(u->dynkomi_mask & color);
	setup_dynkomi(u, b, color);
}

static void
uct_livegfx_hook(engine_t *e)
{
	uct_t *u = e->data;
	/* Hack: Override reportfreq to get decent update rates in GoGui */
	u->reportfreq = MIN(u->reportfreq, 1000);
}

static tree_node_t *
genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive, coord_t *best_coord)
{
	uct_t *u = e->data;
	double time_start = time_now();
	u->pass_all_alive |= pass_all_alive;	

	uct_pondering_stop(u);

	if (u->t && (u->genmove_reset_tree ||
		     (using_dcnn(b) && !(u->t->root->hints & TREE_HINT_DCNN)))) {
		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
	}

	uct_genmove_setup(u, b, color);

        /* Start the Monte Carlo Tree Search! */
	int base_playouts = u->t->root->u.playouts;
	int played_games = uct_search(u, b, ti, color, u->t, false);

	tree_node_t *best;
	best = uct_search_result(u, b, color, u->pass_all_alive, played_games, base_playouts, best_coord);

	if (UDEBUGL(2)) {
		double total_time = time_now() - time_start;
		double mcts_time  = time_now() - u->mcts_time_start + 0.000001; /* avoid divide by zero */
		fprintf(stderr, "genmove in %0.2fs, mcts %0.2fs (%d games/s, %d games/s/thread)\n",
			total_time, mcts_time, (int)(played_games/mcts_time), (int)(played_games/mcts_time/u->threads));
	}

	uct_progress_status(u, u->t, color, played_games, best_coord);

	return best;
}

static coord_t
uct_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{	
	uct_t *u = e->data;

	coord_t best_coord;
	tree_node_t *best = genmove(e, b, ti, color, pass_all_alive, &best_coord);

	/* Pass or resign.
	 * After a pass, pondering is harmful for two reasons:
	 * (i) We might keep pondering even when the game is over.
	 * Of course this is the case for opponent resign as well.
	 * (ii) More importantly, the ownermap will get skewed since
	 * the UCT will start cutting off any playouts. */	
	if (is_pass(best_coord) || is_resign(best_coord)) {
		if (is_pass(best_coord))
			u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
		return best_coord;
	}

	/* Throw away an untrustworthy tree.
	 * Preserve dynamic komi information though, that is important. */
	if (u->t->untrustworthy_tree) {
		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
		uct_prepare_move(u, b, stone_other(color));
	} else
		tree_promote_node(u->t, &best);

	/* Dcnn pondering:
	 * Promoted node wasn't searched with dcnn priors, start from scratch
	 * so it gets dcnn evaluated (and next move as well). Save opponent
	 * best moves from genmove search, will need it later on to guess
	 * next move. */
	if (u->pondering_opt && using_dcnn(b)) {
		int      nbest =  u->dcnn_pondering_mcts;
		coord_t *best_c = u->dcnn_pondering_mcts_c;
		float    best_r[nbest];
		uct_get_best_moves(u, best_c, best_r, nbest, false);
		for (int i = 0; i < nbest; i++)
			if (best_r[i] < 100)  best_c[i] = pass;  /* Too few playouts. */

		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
		uct_prepare_move(u, b, stone_other(color));
	}	

	if (u->pondering_opt && u->t)
		uct_pondering_start(u, b, u->t, stone_other(color), best_coord, true);

	return best_coord;
}

/* Wild pondering for the sake of frontend running Pachi. */
static void
uct_analyze(engine_t *e, board_t *b, enum stone color, int start)
{
	uct_t *u = e->data;

	if (!start) {
		if (u->pondering) uct_pondering_stop(u);
		return;
	}

	/* Start pondering if not already. */
	if (u->pondering)  return;

	if (!u->t)
		uct_prepare_move(u, b, color);

	uct_pondering_start(u, b, u->t, color, 0, false);
}

void
uct_get_best_moves_at(uct_t *u, tree_node_t *parent, coord_t *best_c, float *best_r, int nbest, bool winrates)
{
	tree_node_t* best_d[nbest];
	for (int i = 0; i < nbest; i++)  {
		best_c[i] = pass;  best_r[i] = 0;  best_d[i] = NULL;
	}
	
	/* Find best moves */
	for (tree_node_t *n = parent->children; n; n = n->sibling)
		best_moves_add_full(node_coord(n), n->u.playouts, n, best_c, best_r, (void**)best_d, nbest);

	if (winrates)  /* Get winrates */
		for (int i = 0; i < nbest && best_d[i]; i++)
			best_r[i] = tree_node_get_value(u->t, 1, best_d[i]->u.value);
}

void
uct_get_best_moves(uct_t *u, coord_t *best_c, float *best_r, int nbest, bool winrates)
{
	uct_get_best_moves_at(u, u->t->root, best_c, best_r, nbest, winrates);
}

/* Kindof like uct_genmove() but find the best candidates */
static void
uct_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
	       coord_t *best_c, float *best_r, int nbest)
{
	uct_t *u = e->data;
	uct_pondering_stop(u);
	if (u->t)
		reset_state(u);	
	
	coord_t best_coord;
	genmove(e, b, ti, color, 0, &best_coord);
	uct_get_best_moves(u, best_c, best_r, nbest, true);

	if (u->t)	
		reset_state(u);
}

bool
uct_gentbook(engine_t *e, board_t *b, time_info_t *ti, enum stone color)
{
	uct_t *u = e->data;
	if (!u->t) uct_prepare_move(u, b, color);
	assert(u->t);

	if (ti->dim == TD_GAMES) {
		/* Don't count in games that already went into the tbook. */
		ti->len.games += u->t->root->u.playouts;
	}
	uct_search(u, b, ti, color, u->t, true);

	assert(ti->dim == TD_GAMES);
	tree_save(u->t, b, ti->len.games / 100);

	return true;
}

void
uct_dumptbook(engine_t *e, board_t *b, enum stone color)
{
	uct_t *u = e->data;
	tree_t *t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0,
			 u->max_pruned_size, u->pruning_threshold, u->local_tree_aging, 0);
	tree_load(t, b);
	tree_dump(t, 0);
	tree_done(t);
}


floating_t
uct_evaluate_one(engine_t *e, board_t *b, time_info_t *ti, coord_t c, enum stone color)
{
	uct_t *u = e->data;

	board_t b2;
	board_copy(&b2, b);
	move_t m = { c, color };
	int res = board_play(&b2, &m);
	if (res < 0)
		return NAN;
	color = stone_other(color);

	if (u->t) reset_state(u);
	uct_prepare_move(u, &b2, color);
	assert(u->t);

	floating_t bestval;
	uct_search(u, &b2, ti, color, u->t, true);
	tree_node_t *best = u->policy->choose(u->policy, u->t->root, &b2, color, resign);
	if (!best) {
		bestval = NAN; // the opponent has no reply!
	} else {
		bestval = tree_node_get_value(u->t, 1, best->u.value);
	}

	reset_state(u); // clean our junk

	return isnan(bestval) ? NAN : 1.0f - bestval;
}

void
uct_evaluate(engine_t *e, board_t *b, time_info_t *ti, floating_t *vals, enum stone color)
{
	for (int i = 0; i < b->flen; i++) {
		if (is_pass(b->f[i]))
			vals[i] = NAN;
		else
			vals[i] = uct_evaluate_one(e, b, ti, b->f[i], color);
	}
}

static void
log_nthreads(uct_t *u)
{
	static int logged = 0;
	if (DEBUGL(0) && !logged++)  fprintf(stderr, "Threads: %i\n", u->threads);
}

static size_t
default_max_tree_size()
{
	/* Double it on 64-bit, tree takes up twice as much memory ... */
	int mult = (sizeof(void*) == 4 ? 1 : 2);

	/* Should be enough for most scenarios (up to 240k playouts ...)
	 * If you're using really long thinking times you definitely should
	 * set a higher max_tree_size. */
	return (size_t)300 * mult * 1048576;
}

uct_t *
uct_state_init(char *arg, board_t *b)
{
	uct_t *u = calloc2(1, sizeof(uct_t));
	bool pat_setup = false;
	
	u->debug_level = debug_level;
	u->reportfreq = 1000;
	u->gamelen = MC_GAMELEN;
	u->resign_threshold = 0.2;
	u->sure_win_threshold = 0.95;
	u->mercymin = 0;
	u->significant_threshold = 50;
	u->expand_p = 8;
	u->dumpthres = 0.01;
	u->playout_amaf = true;
	u->amaf_prior = false;
	u->max_tree_size = default_max_tree_size();
	u->fast_alloc = true;
	u->pruning_threshold = 0;
	u->genmove_reset_tree = false;

	u->threads = get_nprocessors();
	u->thread_model = TM_TREEVL;
	u->virtual_loss = 1;

	u->pondering_opt = false;
	u->dcnn_pondering_prior = 5;
	u->dcnn_pondering_mcts = 3;

	u->fuseki_end = 20; // max time at 361*20% = 72 moves (our 36th move, still 99 to play)
	u->yose_start = 40; // (100-40-25)*361/100/2 = 63 moves still to play by us then
	u->bestr_ratio = 0.02;
	// 2.5 is clearly too much, but seems to compensate well for overly stern time allocations.
	// TODO: Further tuning and experiments with better time allocation schemes.
	u->best2_ratio = 2.5;
	// Higher values of max_maintime_ratio sometimes cause severe time trouble in tournaments
	// It might be necessary to reduce it to 1.5 on large board, but more tuning is needed.
	u->max_maintime_ratio = 2.0;

	u->val_scale = 0; u->val_points = 40;
	u->dynkomi_interval = 100;
	u->dynkomi_mask = S_BLACK | S_WHITE;

	u->tenuki_d = 4;
	u->local_tree_aging = 80;
	u->local_tree_depth_decay = 1.5;
	u->local_tree_eval = LTE_ROOT;
	u->local_tree_neival = true;

	u->max_slaves = -1;
	u->slave_index = -1;
	u->stats_delay = 0.01; // 10 ms
	u->shared_levels = 1;

#ifdef PACHI_PLUGINS
	u->plugins = pluginset_init(b);
#endif

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			/** Basic options */

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					u->debug_level = atoi(optval);
				else
					u->debug_level++;
			} else if (!strcasecmp(optname, "reporting") && optval) {
				/* The format of output for detailed progress
				 * information (such as current best move and
				 * its value, etc.). */
				if (!strcasecmp(optval, "text")) {
					/* Plaintext traditional output. */
					u->reporting = UR_TEXT;
				} else if (!strcasecmp(optval, "json")) {
					/* JSON output. Implies debug=0. */
					u->reporting = UR_JSON;
					u->debug_level = 0;
				} else if (!strcasecmp(optval, "jsonbig")) {
					/* JSON output, but much more detailed.
					 * Implies debug=0. */
					u->reporting = UR_JSON_BIG;
					u->debug_level = 0;
				} else if (!strcasecmp(optval, "leelaz")) {
					/* Leela-Zero pondering format. */
					u->reporting = UR_LEELAZ;
				} else
					die("UCT: Invalid reporting format %s\n", optval);
			} else if (!strcasecmp(optname, "reportfreq") && optval) {
				/* The progress information line will be shown
				 * every <reportfreq> simulations. */
				u->reportfreq = atoi(optval);
			} else if (!strcasecmp(optname, "dumpthres") && optval) {
				/* When dumping the UCT tree on output, include
				 * nodes with at least this many playouts.
				 * (A fraction of the total # of playouts at the
				 * tree root.) */
				/* Use 0 to list all nodes with at least one
				 * simulation, and -1 to list _all_ nodes. */
				u->dumpthres = atof(optval);
			} else if (!strcasecmp(optname, "resign_threshold") && optval) {
				/* Resign when this ratio of games is lost
				 * after GJ_MINGAMES sample is taken. */
				u->resign_threshold = atof(optval);
			} else if (!strcasecmp(optname, "sure_win_threshold") && optval) {
				/* Stop reading when this ratio of games is won
				 * after PLAYOUT_EARLY_BREAK_MIN sample is
				 * taken. (Prevents stupid time losses,
				 * friendly to human opponents.) */
				u->sure_win_threshold = atof(optval);
			} else if (!strcasecmp(optname, "force_seed") && optval) {
				/* Set RNG seed at the tree setup. */
				u->force_seed = atoi(optval);
			} else if (!strcasecmp(optname, "no_tbook")) {
				/* Disable UCT opening tbook. */
				u->no_tbook = true;
			} else if (!strcasecmp(optname, "pass_all_alive")) {
				/* Whether to consider passing only after all
				 * dead groups were removed from the board;
				 * this is like all genmoves are in fact
				 * kgs-genmove_cleanup. */
				u->pass_all_alive = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "allow_losing_pass")) {
				/* Whether to consider passing in a clear
				 * but losing situation, to be scored as a loss
				 * for us. */
				u->allow_losing_pass = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "stones_only")) {
				/* Do not count eyes. Nice to teach go to kids.
				 * http://strasbourg.jeudego.org/regle_strasbourgeoise.htm */
				b->rules = RULES_STONES_ONLY;
				u->pass_all_alive = true;
			} else if (!strcasecmp(optname, "debug_after")) {
				/* debug_after=9:1000 will make Pachi think under
				 * the normal conditions, but at the point when
				 * a move is to be chosen, the tree is dumped and
				 * another 1000 simulations are run single-threaded
				 * with debug level 9, allowing inspection of Pachi's
				 * behavior after it has thought a lot. */
				if (optval) {
					u->debug_after.level = atoi(optval);
					char *playouts = strchr(optval, ':');
					if (playouts)
						u->debug_after.playouts = atoi(playouts+1);
					else
						u->debug_after.playouts = 1000;
				} else {
					u->debug_after.level = 9;
					u->debug_after.playouts = 1000;
				}
			} else if ((!strcasecmp(optname, "banner") && optval) ||
				   (!strcasecmp(optname, "comment") && optval)) {
				/* Set message displayed at game start on kgs.
				 * Default is "Pachi %s, Have a nice game !"
				 * '%s' is replaced by Pachi version.
				 * This must come as the last engine parameter.
				 * You can use '+' instead of ' ' if you are wrestling with kgsGtp. */
				if (*next) *--next = ',';
				u->banner = strdup(optval);
				for (char *b = u->banner; *b; b++)
					if (*b == '+') *b = ' ';
				break;
#ifdef PACHI_PLUGINS
			} else if (!strcasecmp(optname, "plugin") && optval) {
				/* Load an external plugin; filename goes before the colon,
				 * extra arguments after the colon. */
				char *pluginarg = strchr(optval, ':');
				if (pluginarg)
					*pluginarg++ = 0;
				plugin_load(u->plugins, optval, pluginarg);
#endif
			/** UCT behavior and policies */

			} else if ((!strcasecmp(optname, "policy")
				/* Node selection policy. ucb1amaf is the
				 * default policy implementing RAVE, while
				 * ucb1 is the simple exploration/exploitation
				 * policy. Policies can take further extra
				 * options. */
			            || !strcasecmp(optname, "random_policy")) && optval) {
				/* A policy to be used randomly with small
				 * chance instead of the default policy. */
				char *policyarg = strchr(optval, ':');
				uct_policy_t **p = !strcasecmp(optname, "policy") ? &u->policy : &u->random_policy;
				if (policyarg)
					*policyarg++ = 0;
				if (!strcasecmp(optval, "ucb1")) {
					*p = policy_ucb1_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1amaf")) {
					*p = policy_ucb1amaf_init(u, policyarg, b);
				} else
					die("UCT: Invalid tree policy %s\n", optval);
			} else if (!strcasecmp(optname, "playout") && optval) {
				/* Random simulation (playout) policy.
				 * moggy is the default policy with large
				 * amount of domain-specific knowledge and
				 * heuristics. light is a simple uniformly
				 * random move selection policy. */
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "moggy")) {
					u->playout = playout_moggy_init(playoutarg, b);
				} else if (!strcasecmp(optval, "light")) {
					u->playout = playout_light_init(playoutarg, b);
				} else
					die("UCT: Invalid playout policy %s\n", optval);
			} else if (!strcasecmp(optname, "prior") && optval) {
				/* Node priors policy. When expanding a node,
				 * it will seed node values heuristically
				 * (most importantly, based on playout policy
				 * opinion, but also with regard to other
				 * things). See uct/prior.c for details.
				 * Use prior=eqex=0 to disable priors. */
				u->prior = uct_prior_init(optval, b, u);
			} else if (!strcasecmp(optname, "mercy") && optval) {
				/* Minimal difference of black/white captures
				 * to stop playout - "Mercy Rule". Speeds up
				 * hopeless playouts at the expense of some
				 * accuracy. */
				u->mercymin = atoi(optval);
			} else if (!strcasecmp(optname, "gamelen") && optval) {
				/* Maximum length of single simulation
				 * in moves. */
				u->gamelen = atoi(optval);
			} else if (!strcasecmp(optname, "expand_p") && optval) {
				/* Expand UCT nodes after it has been
				 * visited this many times. */
				u->expand_p = atoi(optval);
			} else if (!strcasecmp(optname, "random_policy_chance") && optval) {
				/* If specified (N), with probability 1/N, random_policy policy
				 * descend is used instead of main policy descend; useful
				 * if specified policy (e.g. UCB1AMAF) can make unduly biased
				 * choices sometimes, you can fall back to e.g.
				 * random_policy=UCB1. */
				u->random_policy_chance = atoi(optval);

			/** General AMAF behavior */
			/* (Only relevant if the policy supports AMAF.
			 * More variables can be tuned as policy
			 * parameters.) */

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
			} else if (!strcasecmp(optname, "playout_amaf_cutoff") && optval) {
				/* Keep only first N% of playout stage AMAF
				 * information. */
				u->playout_amaf_cutoff = atoi(optval);
			} else if (!strcasecmp(optname, "amaf_prior") && optval) {
				/* In node policy, consider prior values
				 * part of the real result term or part
				 * of the AMAF term? */
				u->amaf_prior = atoi(optval);

			/** Performance and memory management */

			} else if (!strcasecmp(optname, "threads") && optval) {
				/* By default, Pachi will run with only single
				 * tree search thread! */
				u->threads = atoi(optval);
			} else if (!strcasecmp(optname, "thread_model") && optval) {
				if (!strcasecmp(optval, "tree")) {
					/* Tree parallelization - all threads
					 * grind on the same tree. */
					u->thread_model = TM_TREE;
					u->virtual_loss = 0;
				} else if (!strcasecmp(optval, "treevl")) {
					/* Tree parallelization, but also
					 * with virtual losses - this discou-
					 * rages most threads choosing the
					 * same tree branches to read. */
					u->thread_model = TM_TREEVL;
				} else
					die("UCT: Invalid thread model %s\n", optval);
			} else if (!strcasecmp(optname, "virtual_loss") && optval) {
				/* Number of virtual losses added before evaluating a node. */
				u->virtual_loss = atoi(optval);
			} else if (!strcasecmp(optname, "max_tree_size") && optval) {
				/* Maximum amount of memory [MiB] consumed by the move tree.
				 * For fast_alloc it includes the temp tree used for pruning.
				 * Default is 3072 (3 GiB). */
				u->max_tree_size = (size_t)atoll(optval) * 1048576;  /* long is 4 bytes on windows! */
			} else if (!strcasecmp(optname, "fast_alloc")) {
				u->fast_alloc = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "pruning_threshold") && optval) {
				/* Force pruning at beginning of a move if the tree consumes
				 * more than this [MiB]. Default is 10% of max_tree_size.
				 * Increase to reduce pruning time overhead if memory is plentiful.
				 * This option is meaningful only for fast_alloc. */
				u->pruning_threshold = atol(optval) * 1048576;
			} else if (!strcasecmp(optname, "reset_tree")) {
				/* Reset tree before each genmove ?
				 * Default is to reuse previous tree when not using dcnn. 
				 * When using dcnn tree is always reset. */
				u->genmove_reset_tree = !optval || atoi(optval);

			/* Pondering */

			} else if (!strcasecmp(optname, "pondering")) {
				/* Keep searching even during opponent's turn. */
				u->pondering_opt = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "dcnn_pondering_prior") && optval) {
				/* Dcnn pondering: prior guesses for next move.
				 * When pondering with dcnn we need to guess opponent's next move:
				 * Only these guesses are dcnn evaluated.
				 * This is the number of guesses we pick from priors' best moves
				 * (dcnn policy mostly). Default is 5 meaning only top-5 moves
				 * for opponent will be considered (plus dcnnn_pondering_mcts_best
				 * ones, see below). For slow games it makes sense to increase this:
				 * we spend more time before actual search starts but there's more
				 * chance we guess right, so that pondering will be useful. If we
				 * guess wrong search results will be discarded and pondering will
				 * not be useful for this move. For fast games try decreasing it. */
				u->dcnn_pondering_prior = atoi(optval);
			} else if (!strcasecmp(optname, "dcnn_pondering_mcts") && optval) {
				/* Dcnn pondering: mcts guesses for next move.
				 * Same as dcnn_pondering_prior but number of guesses picked from
				 * opponent best moves in genmove search.
				 * Default is 3. */
				size_t n = u->dcnn_pondering_mcts = atoi(optval);
				assert(n <= sizeof(u->dcnn_pondering_mcts_c) / sizeof(u->dcnn_pondering_mcts_c[0]));

			/** Time control */

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
			} else if (!strcasecmp(optname, "max_maintime_ratio") && optval) {
				/* If set and while not in byoyomi, prolong simulating no more than
				 * max_maintime_ratio times the normal desired thinking time. */
				u->max_maintime_ratio = atof(optval);
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

			/** Dynamic komi */

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
					/* You should set dynkomi_mask=1 or a very low
					 * handicap_value for white. */
					u->dynkomi = uct_dynkomi_init_linear(u, dynkomiarg, b);
				} else if (!strcasecmp(optval, "adaptive")) {
					/* There are many more knobs to
					 * crank - see uct/dynkomi.c. */
					u->dynkomi = uct_dynkomi_init_adaptive(u, dynkomiarg, b);
				} else
					die("UCT: Invalid dynkomi mode %s\n", optval);
			} else if (!strcasecmp(optname, "dynkomi_mask") && optval) {
				/* Bitmask of colors the player must be
				 * for dynkomi be applied; the default dynkomi_mask=3 allows
				 * dynkomi even in games where Pachi is white. */
				u->dynkomi_mask = atoi(optval);
			} else if (!strcasecmp(optname, "dynkomi_interval") && optval) {
				/* If non-zero, re-adjust dynamic komi
				 * throughout a single genmove reading,
				 * roughly every N simulations. */
				/* XXX: Does not work with tree
				 * parallelization. */
				u->dynkomi_interval = atoi(optval);
			} else if (!strcasecmp(optname, "extra_komi") && optval) {
				/* Initial dynamic komi settings. This
				 * is useful for the adaptive dynkomi
				 * policy as the value to start with
				 * (this is NOT kept fixed) in case
				 * there is not enough time in the search
				 * to adjust the value properly (e.g. the
				 * game was interrupted). */
				u->initial_extra_komi = atof(optval);

			/** Node value result scaling */

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
			} else if (!strcasecmp(optname, "val_byavg")) {
				/* If true, the score included in the value will
				 * be relative to average score in the current
				 * search episode inst. of jigo. */
				u->val_byavg = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "val_bytemp")) {
				/* If true, the value scaling coefficient
				 * is different based on value extremity
				 * (dist. from 0.5), linear between
				 * val_bytemp_min, val_scale. */
				u->val_bytemp = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "val_bytemp_min") && optval) {
				/* Minimum val_scale in case of val_bytemp. */
				u->val_bytemp_min = atof(optval);

			/** Local trees */
			/* (Purely experimental. Does not work - yet!) */

			} else if (!strcasecmp(optname, "local_tree")) {
				/* Whether to bias exploration by local tree values. */
				u->local_tree = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "tenuki_d") && optval) {
				/* Tenuki distance at which to break the local tree. */
				u->tenuki_d = atoi(optval);
				if (u->tenuki_d > TREE_NODE_D_MAX + 1)
					die("uct: tenuki_d must not be larger than TREE_NODE_D_MAX+1 %d\n", TREE_NODE_D_MAX + 1);
			} else if (!strcasecmp(optname, "local_tree_aging") && optval) {
				/* How much to reduce local tree values between moves. */
				u->local_tree_aging = atof(optval);
			} else if (!strcasecmp(optname, "local_tree_depth_decay") && optval) {
				/* With value x>0, during the descent the node
				 * contributes 1/x^depth playouts in
				 * the local tree. I.e., with x>1, nodes more
				 * distant from local situation contribute more
				 * than nodes near the root. */
				u->local_tree_depth_decay = atof(optval);
			} else if (!strcasecmp(optname, "local_tree_allseq")) {
				/* If disabled, only complete sequences are stored
				 * in the local tree. If this is on, also
				 * subsequences starting at each move are stored. */
				u->local_tree_allseq = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "local_tree_neival")) {
				/* If disabled, local node value is not
				 * computed just based on terminal status
				 * of the coordinate, but also its neighbors. */
				u->local_tree_neival = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "local_tree_eval")) {
				/* How is the value inserted in the local tree
				 * determined. */
				if (!strcasecmp(optval, "root"))
					/* All moves within a tree branch are
					 * considered wrt. their merit
					 * reaching tachtical goal of making
					 * the first move in the branch
					 * survive. */
					u->local_tree_eval = LTE_ROOT;
				else if (!strcasecmp(optval, "each"))
					/* Each move is considered wrt.
					 * its own survival. */
					u->local_tree_eval = LTE_EACH;
				else if (!strcasecmp(optval, "total"))
					/* The tactical goal is the survival
					 * of all the moves of my color and
					 * non-survival of all the opponent
					 * moves. Local values (and their
					 * inverses) are averaged. */
					u->local_tree_eval = LTE_TOTAL;
				else
					die("uct: unknown local_tree_eval %s\n", optval);
			} else if (!strcasecmp(optname, "local_tree_rootchoose")) {
				/* If disabled, only moves within the local
				 * tree branch are considered; the values
				 * of the branch roots (i.e. root children)
				 * are ignored. This may make sense together
				 * with eval!=each, we consider only moves
				 * that influence the goal, not the "rating"
				 * of the goal itself. (The real solution
				 * will be probably using criticality to pick
				 * local tree branches.) */
				u->local_tree_rootchoose = !optval || atoi(optval);

			/** Other heuristics */
			} else if (!strcasecmp(optname, "patterns")) {
				/* Load pattern database. Various modules
				 * (priors, policies etc.) may make use
				 * of this database. They will request
				 * it automatically in that case, but you
				 * can use this option to tweak the pattern
				 * parameters. */
				patterns_init(&u->pc, optval, false, true);
				pat_setup = true;
			} else if (!strcasecmp(optname, "significant_threshold") && optval) {
				/* Some heuristics (XXX: none in mainline) rely
				 * on the knowledge of the last "significant"
				 * node in the descent. Such a node is
				 * considered reasonably trustworthy to carry
				 * some meaningful information in the values
				 * of the node and its children. */
				u->significant_threshold = atoi(optval);

			/** Distributed engine slaves setup */
#ifdef DISTRIBUTED
			} else if (!strcasecmp(optname, "slave")) {
				/* Act as slave for the distributed engine. */
				u->slave = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "slave_index") && optval) {
				/* Optional index if per-slave behavior is desired.
				 * Must be given as index/max */
				u->slave_index = atoi(optval);
				char *p = strchr(optval, '/');
				if (p) u->max_slaves = atoi(++p);
			} else if (!strcasecmp(optname, "shared_nodes") && optval) {
				/* Share at most shared_nodes between master and slave at each genmoves.
				 * Must use the same value in master and slaves. */
				u->shared_nodes = atoi(optval);
			} else if (!strcasecmp(optname, "shared_levels") && optval) {
				/* Share only nodes of level <= shared_levels. */
				u->shared_levels = atoi(optval);
			} else if (!strcasecmp(optname, "stats_hbits") && optval) {
				/* Set hash table size to 2^stats_hbits for the shared stats. */
				u->stats_hbits = atoi(optval);
			} else if (!strcasecmp(optname, "stats_delay") && optval) {
				/* How long to wait in slave for initial stats to build up before
				 * replying to the genmoves command (in ms) */
				u->stats_delay = 0.001 * atof(optval);
#endif /* DISTRIBUTED */

			/** Presets */

			} else if (!strcasecmp(optname, "maximize_score")) {
				/* A combination of settings that will make
				 * Pachi try to maximize his points (instead
				 * of playing slack yose) or minimize his loss
				 * (and proceed to counting even when losing). */
				/* Please note that this preset might be
				 * somewhat weaker than normal Pachi, and the
				 * score maximization is approximate; point size
				 * of win/loss still should not be used to judge
				 * strength of Pachi or the opponent. */
				/* See README for some further notes. */
				if (!optval || atoi(optval)) {
					/* Allow scoring a lost game. */
					u->allow_losing_pass = true;
					/* Make Pachi keep his calm when losing
					 * and/or maintain winning marging. */
					/* Do not play games that are losing
					 * by too much. */
					/* XXX: komi_ratchet_age=40000 is necessary
					 * with losing_komi_ratchet, but 40000
					 * is somewhat arbitrary value. */
					char dynkomi_args[] = "losing_komi_ratchet:komi_ratchet_age=60000:no_komi_at_game_end=0:max_losing_komi=30";
					u->dynkomi = uct_dynkomi_init_adaptive(u, dynkomi_args, b);
					/* XXX: Values arbitrary so far. */
					/* XXX: Also, is bytemp sensible when
					 * combined with dynamic komi?! */
					u->val_scale = 0.01;
					u->val_bytemp = true;
					u->val_bytemp_min = 0.001;
					u->val_byavg = true;
				}

			} else
				die("uct: Invalid engine argument %s or missing value\n", optname);
		}
	}

	if (!u->policy)
		u->policy = policy_ucb1amaf_init(u, NULL, b);

	if (!!u->random_policy_chance ^ !!u->random_policy)
		die("uct: Only one of random_policy and random_policy_chance is set\n");

	if (!u->local_tree) {
		/* No ltree aging. */
		u->local_tree_aging = 1.0f;
	}

	if (u->fast_alloc) {
		if (u->pruning_threshold < u->max_tree_size / 10)
			u->pruning_threshold = u->max_tree_size / 10;
		if (u->pruning_threshold > u->max_tree_size / 2)
			u->pruning_threshold = u->max_tree_size / 2;

		/* Limit pruning temp space to 20% of memory. Beyond this we discard
		 * the nodes and recompute them at the next move if necessary. */
		u->max_pruned_size = u->max_tree_size / 5;
		u->max_tree_size -= u->max_pruned_size;
	} else {
		/* Reserve 5% memory in case the background free() are slower
		 * than the concurrent allocations. */
		u->max_tree_size -= u->max_tree_size / 20;
	}

	dcnn_init(b);
	if (!using_dcnn(b))		joseki_load(b->size);
	if (!pat_setup)			patterns_init(&u->pc, NULL, false, true);
	log_nthreads(u);
	if (!u->prior)			u->prior = uct_prior_init(NULL, b, u);
	if (!u->playout)		u->playout = playout_moggy_init(NULL, b);
	if (!u->playout->debug_level)	u->playout->debug_level = u->debug_level;

	if (u->slave) {
		if (!u->stats_hbits) u->stats_hbits = DEFAULT_STATS_HBITS;
		if (!u->shared_nodes) u->shared_nodes = DEFAULT_SHARED_NODES;
		assert(u->shared_levels * board_bits2(b) <= 8 * (int)sizeof(path_t));
	}

	if (!u->dynkomi)		u->dynkomi = uct_dynkomi_init_linear(u, NULL, b);
	if (!u->banner)                 u->banner = strdup("Pachi %s, Have a nice game !");

	/* Some things remain uninitialized for now - the opening tbook
	 * is not loaded and the tree not set up. */
	/* This will be initialized in setup_state() at the first move
	 * received/requested. This is because right now we are not aware
	 * about any komi or handicap setup and such. */

	return u;
}

void
engine_uct_init(engine_t *e, char *arg, board_t *b)
{
	uct_t *u = uct_state_init(arg, b);
	e->name = "UCT";
	e->board_print = uct_board_print;
	e->notify_play = uct_notify_play;
	e->chat = uct_chat;
	e->result = uct_result;
	e->genmove = uct_genmove;
#ifdef DISTRIBUTED
	e->genmoves = uct_genmoves;
	if (u->slave)
		e->notify = uct_notify;
#endif
	e->best_moves = uct_best_moves;
	e->evaluate = uct_evaluate;
	e->analyze = uct_analyze;
	e->dead_group_list = uct_dead_group_list;
	e->stop = uct_stop;
	e->done = uct_done;
	e->ownermap = uct_ownermap;
	e->livegfx_hook = uct_livegfx_hook;
	e->data = u;
	e->comment = u->banner;
}
