#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEBUG

#include "debug.h"
#include "pachi.h"
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
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"
#include "dcnn.h"

#ifdef DISTRIBUTED
#include "uct/slave.h"
#endif

uct_policy_t *policy_ucb1_init(uct_t *u, char *arg);
uct_policy_t *policy_ucb1amaf_init(uct_t *u, char *arg, board_t *board);
static void uct_pondering_start(uct_t *u, board_t *b0, tree_t *t, enum stone color, coord_t our_move, int flags);
static void uct_genmove_pondering_save_replies(uct_t *u, board_t *b, enum stone color, coord_t next_move);
static void uct_genmove_pondering_start(uct_t *u, board_t *b, enum stone color, coord_t our_move);

/* Maximal simulation length. */
#define MC_GAMELEN	MAX_GAMELEN

static void
setup_state(uct_t *u, board_t *b, enum stone color)
{
	size_t size = u->tree_size;
	if (DEBUGL(3)) fprintf(stderr, "allocating %i Mb for search tree\n", (int)(size / (1024*1024)));
	u->t = tree_init(color, size, stats_hbits(u));
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
	if (UDEBUGL(3)) fprintf(stderr, "resetting tree\n");
	assert(u->t);
	tree_done(u->t);
	u->t = NULL;
}

static void
setup_dynkomi(uct_t *u, board_t *b, enum stone to_play)
{
	if (u->t->use_extra_komi && !pondering(u) && u->dynkomi->permove)
		u->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, u->t);
	else if (!u->t->use_extra_komi)
		u->t->extra_komi = 0;
}

void
uct_prepare_move(uct_t *u, board_t *b, enum stone color)
{
	if (u->t) {  /* Verify that we have sane state. */
		assert(u->t && b->moves);
		assert(node_coord(u->t->root) == last_move(b).coord);
		assert(u->t->root_color == last_move(b).color);
		if (color != stone_other(u->t->root_color))
			die("Fatal: Non-alternating play detected %d %d\n", color, u->t->root_color);
#ifdef DISTRIBUTED
		uct_htable_reset(u->t);
#endif
	} else  /* We need fresh state. */
		setup_state(u, b, color);

	ownermap_init(&u->ownermap);
	u->allow_pass = (b->moves > board_earliest_pass(b));  /* && dames < 10  if using patterns */
#ifdef DISTRIBUTED
	u->played_own = u->played_all = 0;
#endif
}

static bool pass_is_safe(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, char **msg, bool log,
			 move_queue_t *dead, move_queue_t *dead_extra, move_queue_t *unclear,
			 bool unclear_kludge, char *label);
static bool pass_is_safe_(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, char **msg,
			  move_queue_t *dead, move_queue_t *unclear, bool unclear_kludge);

/* Does the board look like a final position, and do we win counting ?
 * (if allow_losing_pass was set we don't care about score)
 * If true, returns dead groups used to evaluate position in @dead
 * (possibly guessed if there are unclear groups). */
bool
uct_pass_is_safe(uct_t *u, board_t *b, enum stone color, bool pass_all_alive,
		 move_queue_t *dead, char **msg, bool log)
{
	mq_init(dead);
	
	/* Check this early, no need to go through the whole thing otherwise. */
	*msg = "too early to pass";
	if (b->moves < board_earliest_pass(b))
		return false;
	
	/* Make sure enough playouts are simulated to get a reasonable dead group list. */
	move_queue_t dead_orig;
	move_queue_t unclear_orig;
	uct_mcowner_playouts(u, b, color);
	ownermap_dead_groups(b, &u->ownermap, &dead_orig, &unclear_orig);

#define init_pass_is_safe_groups()  do {	\
		unclear = unclear_orig;		\
		*dead = dead_orig;		\
		mq_init(&dead_extra);		\
	} while (0)

	move_queue_t unclear;
	move_queue_t dead_extra;
	init_pass_is_safe_groups();
	
	if (DEBUGL(2) && log && unclear.moves)  mq_print_line("unclear groups: ", &unclear);

	/* Guess unclear groups ?
	 * By default Pachi is fairly pedantic at the end of the game and will 
	 * refuse to pass until everything is nice and clear to him. This can 
	 * take some moves depending on the situation if there are unclear
	 * groups. Guessing allows more user-friendly behavior, passing earlier
	 * without having to clarify everything. Under japanese rules this can
	 * also prevent him from losing the game if clarifying would cost too
	 * many points.
	 * Even though Pachi will only guess won positions there is a possibility
	 * of getting dead group status wrong, so only ok if game setup asks
	 * players for dead stones and game can resume in case of disagreement
	 * (auto-scored games like on ogs for example are definitely not ok).
	 * -> Only enabled if user asked for it or playing japanese on kgs
	 * (for chinese don't risk screwing up and ending up in cleanup phase) */
	bool guess_unclear_ok = pachi_options()->guess_unclear_groups;
	if (pachi_options()->kgs)
		guess_unclear_ok = (b->rules == RULES_JAPANESE);

	/* smart pass: try worst-case scenario first:
	 * own unclear groups are dead and opponent's are alive.
	 * If we still win this way for sure it's ok. */
	if (guess_unclear_ok && unclear.moves) {
		for (unsigned int i = 0; i < unclear.moves; i++)
			if (board_at(b, unclear.move[i]) == color)
				mq_add(&dead_extra, unclear.move[i], 0);    /* own groups -> dead */
		unclear.moves = 0;                                          /* opponent's groups -> alive */
		if (pass_is_safe(u, b, color, pass_all_alive, msg, log,
				 dead, &dead_extra, &unclear, true, "(worst-case)"))
			return true;
		init_pass_is_safe_groups();  /* revert changes */
	}

	/* Strict mode then: don't pass until everything is clarified. */
	return pass_is_safe(u, b, color, pass_all_alive, msg, log,
			    dead, &dead_extra, &unclear, false, "");
}

static bool
pass_is_safe(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, char **msg, bool log,
	     move_queue_t *dead, move_queue_t *dead_extra, move_queue_t *unclear,
	     bool unclear_kludge, char *label)
{
	move_queue_t guessed;  guessed = *dead_extra;
	mq_append(dead, dead_extra);
	
	bool r = pass_is_safe_(u, b, color, pass_all_alive, msg, dead, unclear, unclear_kludge);

	/* smart pass: log guessed unclear groups if successful    (DEBUGL(2)) */
	if (unclear_kludge && log && r && DEBUGL(2) && !DEBUGL(3)) {
		mq_print("pass ok assuming dead: ", &guessed);
		fprintf(stderr, "%s\n", label);
	}

	/* smart pass: log failed attempts as well                 (DEBUGL(3)) */
	if (unclear_kludge && log && DEBUGL(3)) { /* log everything */
		fprintf(stderr, "  pass %s ", (r ? "ok" : "no"));
		int n = 0;
		n += mq_print("assuming dead: ", &guessed);
		fprintf(stderr, "%*s -> %-7s %s %s\n", abs(50-n), "",
			board_official_score_str(b, dead), (r ? "" : *msg), label);
	}
	
	return r;
}

static bool
pass_is_safe_(uct_t *u, board_t *b, enum stone color, bool pass_all_alive, char **msg,
	      move_queue_t *dead, move_queue_t *unclear, bool unclear_kludge)
{
	bool check_score = !u->allow_losing_pass;

	if (pass_all_alive) {  /* kgs chinese rules cleanup phase */
		*msg = "need to remove opponent dead groups first";
		for (unsigned int i = 0; i < dead->moves; i++)
			if (board_at(b, dead->move[i]) == stone_other(color))
				return false;
		dead->moves = 0; // our dead stones are alive when pass_all_alive is true

		float final_score = board_official_score_color(b, dead, color);
		*msg = "losing on official score";
		return (check_score ? final_score >= 0 : true);
	}
	
	int final_ownermap[board_max_coords(b)];
	int dame, seki;
	floating_t final_score = board_official_score_details(b, dead, &dame, &seki, final_ownermap, &u->ownermap);
	if (color == S_BLACK)  final_score = -final_score;

	floating_t score_est;
	if (unclear_kludge)
		score_est = final_score;   /* unclear groups, can't trust score est ... */
	else {
		/* Check score estimate first, official score is off if position is not final */
		*msg = "losing on score estimate";
		score_est = ownermap_score_est_color(b, &u->ownermap, color);
		if (check_score && score_est < 0)  return false;
	}
	
	/* Don't go to counting if position is not final. */
	if (!board_position_final_full(b, &u->ownermap, dead, unclear, score_est,
				       final_ownermap, dame, final_score, msg))
		return false;

	*msg = "losing on official score";
	return (check_score ? final_score >= 0 : true);
}

static void
uct_board_print(engine_t *e, board_t *b, FILE *f)
{
	uct_t *u = (uct_t*)e->data;
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
		board_done(&b2);
	}
}

static ownermap_t*
uct_ownermap(engine_t *e, board_t *b)
{
	uct_t *u = (uct_t*)e->data;
	
	/* Make sure ownermap is well-seeded. */
	uct_mcowner_playouts(u, b, board_to_play(b));
	
	return &u->ownermap;
}

static char *
uct_notify_play(engine_t *e, board_t *b, move_t *m, char *enginearg, bool *print_board)
{
	uct_t *u = (uct_t*)e->data;
	bool was_searching = thread_manager_running;
	
	if (!u->t) {
		/* No state, create one - this is probably game beginning
		 * and we need to load the opening tbook right now. */
		uct_prepare_move(u, b, m->color);
		assert(u->t);
	}

	/* Stop pondering, required by tree_promote_move() */
	uct_pondering_stop(u);
	
	if (u->slave && was_searching && m->color == u->my_color) {
		if (DEBUGL(1) && debug_boardprint)  *print_board = true;
		if (UDEBUGL(3)) tree_dump(u->t, u->dumpthres);
	}

	if (is_resign(m->coord)) {  /* Reset state. */
		reset_state(u);
		return NULL;
	}

	/* Save best replies before resetting tree (dcnn pondering). */
	if (u->slave && u->pondering_opt)
		uct_genmove_pondering_save_replies(u, b, m->color, m->coord);
	
	/* Promote node of the appropriate move to the tree root.
	 * If using dcnn, only promote node if it has dcnn priors:
	 * Direction of tree search is heavily influenced by initial priors,
	 * if we started searching without dcnn data better start from scratch. */
	enum promote_reason reason;
	assert(u->t->root);
	if (!tree_promote_move(u->t, m, b, &reason)) {
		if (UDEBUGL(3)) {
			if      (reason == PROMOTE_UNTRUSTWORTHY)  fprintf(stderr, "Not promoting move node in untrustworthy tree.\n");
			else if (reason == PROMOTE_DCNN_MISSING)   fprintf(stderr, "Played move has no dcnn priors, resetting tree.\n");
			else					   fprintf(stderr, "Warning: Cannot promote move node! Several play commands in row?\n");
		}

		/* Preserve dynamic komi information, though, that is important. */
		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
	}

	/* If we are a slave in a distributed engine, start pondering once
	 * we know which move we actually played. See uct_genmove() about
	 * the check for pass. */
	if (u->slave && u->pondering_opt && m->color == u->my_color && !is_pass(m->coord))
		uct_genmove_pondering_start(u, b, m->color, m->coord);

	return NULL;
}

static char *
uct_result(engine_t *e, board_t *b)
{
	uct_t *u = (uct_t*)e->data;
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
	uct_t *u = (uct_t*)e->data;

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
uct_dead_groups(engine_t *e, board_t *b, move_queue_t *dead)
{
	uct_t *u = (uct_t*)e->data;
	
	/* This means the game is probably over, no use pondering on. */
	uct_pondering_stop(u);
	
	if (u->pass_all_alive)
		return; // no dead groups

	/* Normally last genmove was a pass and we've already figured out dead groups.
	 * Don't recompute dead groups here, result could be different this time and
	 * lead to bad result ! */
	if (u->pass_moveno == b->moves || u->pass_moveno == b->moves - 1) {
		memcpy(dead, &u->dead_groups, sizeof(*dead));
		return;
	}

	if (UDEBUGL(1)) fprintf(stderr, "WARNING: Recomputing dead groups\n");

	/* Make sure the ownermap is well-seeded. */
	uct_mcowner_playouts(u, b, S_BLACK);
	if (UDEBUGL(2))  board_print_ownermap(b, stderr, &u->ownermap);

	ownermap_dead_groups(b, &u->ownermap, dead, NULL);
}

static void
uct_stop(engine_t *e)
{
	/* This is called on game over notification. However, an undo
	 * and game resume can follow, so don't panic yet and just
	 * relax and stop thinking so that we don't waste CPU. */
	uct_t *u = (uct_t*)e->data;
	uct_pondering_stop(u);
}

/* This is called on engine reset, especially when clear_board
 * is received and new game should begin. */
static void
uct_done(engine_t *e)
{
	uct_t *u = (uct_t*)e->data;

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
	uct_search_start(u, b, color, t, ti, &s, 0);
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

		if (s.fullmem && u->auto_alloc) {
			/* Stop search, realloc tree and restart search */
			if (uct_search_realloc_tree(u, b, color, ti, &s))  continue;
			break;
		}
		
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
		uct_progress_status(u, t, b, color, 0, NULL);

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

#ifdef DISTRIBUTED
	u->played_own += ctx->games;
#endif
	return ctx->games;
}

/* Dcnn pondering:
 * Next move wasn't searched with dcnn priors, search will start from scratch
 * so it gets dcnn evaluated (and next move as well). Save opponent best replies
 * from search before tree gets reset, will need it later to guess next move.
 * Call order must be:
 *     uct_genmove_pondering_save_replies()
 *     tree_promote_node()
 *     uct_genmove_pondering_start() */
static void
uct_genmove_pondering_save_replies(uct_t *u, board_t *b, enum stone color, coord_t next_move)
{
	if (!(u->pondering_opt && using_dcnn(b)))  return;
	
	int      nbest =  u->dcnn_pondering_mcts;
	coord_t *best_c = u->dcnn_pondering_mcts_c;
	float    best_r[nbest];
	for (int i = 0; i < nbest; i++)
		best_c[i] = pass;
	
	if (!(u->t && color == stone_other(u->t->root_color)))  return;
	tree_node_t *best = tree_get_node(u->t->root, next_move);
	if (!best)  return;
	uct_get_best_moves_at(u, best, best_c, best_r, nbest, false, 100);
}

/* Start pondering at the end of genmove.
 * Must call uct_genmove_pondering_save_replies() before. */
static void
uct_genmove_pondering_start(uct_t *u, board_t *b, enum stone color, coord_t our_move)
{
	enum stone other_color = stone_other(color);

	if (!u->t)  uct_prepare_move(u, b, other_color);
	
	uct_pondering_start(u, b, u->t, other_color, our_move, UCT_SEARCH_GENMOVE_PONDERING | UCT_SEARCH_WANT_GC);
}

/* Start pondering in the background with @color to play.
 * @our_move	move to be added before starting. 0 means doesn't apply.
 * @flags	uct_search_start() flags for this search */
static void
uct_pondering_start(uct_t *u, board_t *b0, tree_t *t, enum stone color, coord_t our_move, int flags)
{
	if (UDEBUGL(1))
		fprintf(stderr, "Starting to ponder with color %s\n", stone2str(stone_other(color)));
	flags |= UCT_SEARCH_PONDERING;
	u->search_flags = flags;

	/* We need a local board copy to ponder upon. */
	board_t *b = malloc2(board_t); board_copy(b, b0);

	/* Board needs updating ? (b0 did not have the genmove'd move played yet) */
	if (our_move) {	          /* 0 never a real coord */
		move_t m = move(our_move, stone_other(color));
		int res = board_play(b, &m);
		assert(res >= 0);
	}
	/* analyzing should be only case of switching color to play */
	if (genmove_pondering(u))  assert(color == board_to_play(b));
	
	setup_dynkomi(u, b, color);

	/* Start MCTS manager thread "headless". */
	static uct_search_state_t s;
	uct_search_start(u, b, color, t, NULL, &s, flags);
}

/* uct_search_stop() frontend for the pondering (non-genmove) mode, and
 * to stop the background search for a slave in the distributed engine. */
void
uct_pondering_stop(uct_t *u)
{
	if (!thread_manager_running)
		return;

	/* Search active but not pondering actually ? Stop search.
	 * Distributed mode slaves need that, special case. */
	if (!pondering(u)) {  uct_search_stop();  return;  }

	/* Stop the thread manager. */
	uct_thread_ctx_t *ctx = uct_search_stop();  /* clears search flags */
	
	if (UDEBUGL(1))  uct_progress_status(u, ctx->t, ctx->b, ctx->color, 0, NULL);

	free(ctx->b);
	u->reporting = u->reporting_opt;
	u->report_fh = stderr;
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

static tree_node_t *
genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive, coord_t *best_coord)
{
	uct_t *u = (uct_t*)e->data;
	double time_start = time_now();
	u->pass_all_alive |= pass_all_alive;
	u->mcts_time = 0;

	uct_pondering_stop(u);

	if (u->t) {
		bool unexpected_color = (color != board_to_play(b));  /* playing twice in a row ?? */
		bool missing_dcnn_priors = (using_dcnn(b) && !(u->t->root->hints & TREE_HINT_DCNN));
		if (u->genmove_reset_tree || u->t->untrustworthy_tree ||
		    unexpected_color || missing_dcnn_priors) {
			u->initial_extra_komi = u->t->extra_komi;
			reset_state(u);
		}
	}

	uct_genmove_setup(u, b, color);

        /* Start the Monte Carlo Tree Search! */
	int base_playouts = u->t->root->u.playouts;
	int played_games = uct_search(u, b, ti, color, u->t, false);

	tree_node_t *best;
	best = uct_search_result(u, b, color, u->pass_all_alive, played_games, base_playouts, best_coord);

	if (UDEBUGL(2)) {
		double total_time = time_now() - time_start;
		double mcts_time  = u->mcts_time + 0.000001; /* avoid divide by zero */
		fprintf(stderr, "genmove in %0.2fs, mcts %0.2fs (%d games/s, %d games/s/thread)\n",
			total_time, mcts_time, (int)(played_games/mcts_time), (int)(played_games/mcts_time/u->threads));
	}

	uct_progress_status(u, u->t, b, color, 0, best_coord);

	return best;
}

static coord_t
uct_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	uct_t *u = (uct_t*)e->data;

	coord_t best;
	tree_node_t *best_node = genmove(e, b, ti, color, pass_all_alive, &best);

	/* Pass or resign.
	 * After a pass, pondering is harmful for two reasons:
	 * (i) We might keep pondering even when the game is over.
	 * Of course this is the case for opponent resign as well.
	 * (ii) More importantly, the ownermap will get skewed since
	 * the UCT will start cutting off any playouts. */	
	if (is_pass(best) || is_resign(best)) {
		if (is_pass(best))
			u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
		return best;
	}

	/* Save best replies before resetting tree (dcnn pondering). */
	if (u->pondering_opt)
		uct_genmove_pondering_save_replies(u, b, color, best);
	
	/* Promote node or throw away tree as needed.
	 * Reset now if we don't reuse tree, avoids unnecessary tree gc. */
	if (!reusing_tree(u, b) ||
	    !tree_promote_node(u->t, best_node, b, NULL)) {
		/* Preserve dynamic komi information though, that is important. */
		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
	}

	if (u->pondering_opt)
		uct_genmove_pondering_start(u, b, color, best);
	return best;
}

/* lz-genmove_analyze */
static coord_t
uct_genmove_analyze(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	uct_t *u = (uct_t*)e->data;

	uct_pondering_stop(u);   /* Don't clobber report_fh later on... */
	
	u->reporting = UR_LEELA_ZERO;
	u->report_fh = stdout;	
	coord_t coord = uct_genmove(e, b, ti, color, pass_all_alive);
	u->reporting = u->reporting_opt;
	u->report_fh = stderr;
	
	return coord;
}

/* Start tree search in the background and output stats for the sake of
 * frontend running Pachi: sortof like pondering without a genmove.
 * Stop processing if @start is 0. */
static void
uct_analyze(engine_t *e, board_t *b, enum stone color, int start)
{
	uct_t *u = (uct_t*)e->data;
	int flags = (pondering(u) ? u->search_flags : 0);
	bool genmove_pondering = genmove_pondering(u);
	
	if (!start) {
		if (pondering(u))  uct_pondering_stop(u);	/* clears flags ! */
		if (genmove_pondering)  /* pondering + analyzing ? resume normal pondering */
			uct_pondering_start(u, b, u->t, board_to_play(b), 0, flags);
		return;
	}

	/* If pondering already restart, situation/parameters may have changed.
	 * For example frequency change or getting analyze cmd while pondering. */
	if (pondering(u))
		uct_pondering_stop(u);
	
	if (u->t) {
		bool missing_dcnn_priors = (using_dcnn(b) && !(u->t->root->hints & TREE_HINT_DCNN));
		bool switching_color_to_play = (color != stone_other(u->t->root_color));
		if (missing_dcnn_priors || switching_color_to_play)
			reset_state(u);
	}
	
	u->reporting = UR_LEELA_ZERO;
	u->report_fh = stdout;          /* Reset in uct_pondering_stop() */
	if (!u->t)  uct_prepare_move(u, b, color);
	uct_pondering_start(u, b, u->t, color, 0, flags);
}

/* Same as uct_get_best_moves() for node @parent.
 * XXX pass can be a valid move in which case you need best_n to check. 
 *     have another function which exposes best_n ? */
void
uct_get_best_moves_at(uct_t *u, tree_node_t *parent, coord_t *best_c, float *best_r, int nbest,
		      bool winrates, int min_playouts)
{
	tree_node_t* best_n[nbest];
	for (int i = 0; i < nbest; i++)  {
		best_c[i] = pass;  best_r[i] = 0;  best_n[i] = NULL;
	}
	
	/* Find best moves */
	for (tree_node_t *n = parent->children; n; n = n->sibling)
		if (n->u.playouts >= min_playouts)
			best_moves_add_full(node_coord(n), n->u.playouts, n, best_c, best_r, (void**)best_n, nbest);

	if (winrates)  /* Get winrates */
		for (int i = 0; i < nbest && best_n[i]; i++)
			best_r[i] = tree_node_get_value(u->t, 1, best_n[i]->u.value);
}

/* Get best moves with at least @min_playouts.
 * If @winrates is true @best_r returns winrates instead of visits.
 * (moves remain in best-visited order) */
void
uct_get_best_moves(uct_t *u, coord_t *best_c, float *best_r, int nbest, bool winrates, int min_playouts)
{
	uct_get_best_moves_at(u, u->t->root, best_c, best_r, nbest, winrates, min_playouts);
}

/* Kindof like uct_genmove() but find the best candidates */
static void
uct_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
	       coord_t *best_c, float *best_r, int nbest)
{
	uct_t *u = (uct_t*)e->data;
	uct_pondering_stop(u);
	if (u->t)
		reset_state(u);	
	
	coord_t best_coord;
	genmove(e, b, ti, color, 0, &best_coord);
	uct_get_best_moves(u, best_c, best_r, nbest, true, 100);

	if (u->t)	
		reset_state(u);
}

bool
uct_gentbook(engine_t *e, board_t *b, time_info_t *ti, enum stone color)
{
	uct_t *u = (uct_t*)e->data;
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
	uct_t *u = (uct_t*)e->data;
	size_t size = u->tree_size;
	tree_t *t = tree_init(color, size, 0);
	tree_load(t, b);
	tree_dump(t, 0);
	tree_done(t);
}


floating_t
uct_evaluate_one(engine_t *e, board_t *b, time_info_t *ti, coord_t c, enum stone color)
{
	uct_t *u = (uct_t*)e->data;

	board_t b2;
	board_copy(&b2, b);
	move_t m = { c, color };
	int res = board_play(&b2, &m);
	if (res < 0)
		return NAN;
	color = stone_other(color);

	if (u->t) reset_state(u);
	uct_genmove_setup(u, &b2, color);
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

size_t
uct_default_tree_size()
{
	/* Double it on 64-bit, tree takes up twice as much memory ... */
	int mult = (sizeof(void*) == 4 ? 1 : 2);

	/* Default tree size can be small now that it grows as needed. */
	return (size_t)100 * mult * 1048576;
}

/* Set current tree size to use taking memory limits into account */
void
uct_tree_size_init(uct_t *u, size_t tree_size)
{
	size_t max_tree_size = (u->max_tree_size_opt ? u->max_tree_size_opt : (size_t)-1);
	size_t max_mem = (u->max_mem ? u->max_mem : (size_t)-1);

	/* fixed_mem: can use either "tree_size" or "max_tree_size"
	 * to set amount of memory to allocate.
	 * before auto_alloc there was only max_tree_size so you
	 * can "fixed_mem,max_tree_size=..." to get old behavior. */
	if (!u->auto_alloc && u->max_tree_size_opt)
		tree_size = u->max_tree_size_opt;
	
	/* Honor memory limits */
	if (tree_size > max_tree_size)	tree_size = max_tree_size;
	if (tree_size > max_mem)	tree_size = max_mem;
	
	u->tree_size = tree_size;
}


#define NEED_RESET   ENGINE_SETOPTION_NEED_RESET
#define option_error engine_setoption_error

static bool
uct_setoption(engine_t *e, board_t *b, const char *optname, char *optval,
	      char **err, bool setup, bool *reset)
{
	static_strbuf(ebuf, 256);
	uct_t *u = (uct_t*)e->data;

	/** Basic options */

	if (!strcasecmp(optname, "debug")) {
		if (optval)  u->debug_level = atoi(optval);
		else         u->debug_level++;
	}
	else if (!strcasecmp(optname, "reporting") && optval) {
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
		} else if (!strcasecmp(optval, "leela-zero") ||
			   !strcasecmp(optval, "leelaz") ||
			   !strcasecmp(optval, "lz")) {
			/* Leela-Zero pondering format. */
			u->reporting = UR_LEELA_ZERO;
		} else
			option_error("UCT: Invalid reporting format %s\n", optval);
		u->reporting_opt = u->reporting;  /* original value */
	}
	else if (!strcasecmp(optname, "reportfreq") && optval) {
		/* Set search progress info frequency:
		 *   reportfreq=<int>    show progress every <int> playouts.
		 *   reportfreq=<float>  show progress every <float> seconds.
		 *   reportfreq=1s       show progress every second. */
		u->reportfreq_time = 0.0;
		u->reportfreq_playouts = 0;
		if (strchr(optval, '.') || strchr(optval, 's'))
			u->reportfreq_time = atof(optval);
		else
			u->reportfreq_playouts = atoi(optval);
	}
	else if (!strcasecmp(optname, "dumpthres") && optval) {
		/* When dumping the UCT tree on output, include
		 * nodes with at least this many playouts.
		 * (A fraction of the total # of playouts at the
		 * tree root.) */
		/* Use 0 to list all nodes with at least one
		 * simulation, and -1 to list _all_ nodes. */
		u->dumpthres = atof(optval);
	}
	else if (!strcasecmp(optname, "resign_threshold") && optval) {
		/* Resign when this ratio of games is lost
		 * after GJ_MINGAMES sample is taken. */
		u->resign_threshold = atof(optval);
	}
	else if (!strcasecmp(optname, "sure_win_threshold") && optval) {
		/* Stop reading when this ratio of games is won
		 * after PLAYOUT_EARLY_BREAK_MIN sample is
		 * taken. (Prevents stupid time losses,
		 * friendly to human opponents.) */
		u->sure_win_threshold = atof(optval);
	}
	else if (!strcasecmp(optname, "force_seed") && optval) {
		/* Set RNG seed at the tree setup. */
		u->force_seed = atoi(optval);
	}
	else if (!strcasecmp(optname, "no_tbook")) {
		/* Disable UCT opening tbook. */
		u->no_tbook = true;
	}
	else if (!strcasecmp(optname, "pass_all_alive")) {
		/* Whether to consider passing only after all
		 * dead groups were removed from the board;
		 * this is like all genmoves are in fact
		 * kgs-genmove_cleanup. */
		u->pass_all_alive = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "allow_losing_pass")) {
		/* Whether to consider passing in a clear
		 * but losing situation, to be scored as a loss
		 * for us. */
		u->allow_losing_pass = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "stones_only")) {
		/* Do not count eyes. Nice to teach go to kids.
		 * http://strasbourg.jeudego.org/regle_strasbourgeoise.htm */
		b->rules = RULES_STONES_ONLY;
		u->pass_all_alive = true;
	}
	else if (!strcasecmp(optname, "debug_after")) {
		/* debug_after=9:1000 will make Pachi think under
		 * the normal conditions, but at the point when
		 * a move is to be chosen, the tree is dumped and
		 * another 1000 simulations are run single-threaded
		 * with debug level 9, allowing inspection of Pachi's
		 * behavior after it has thought a lot. */
		if (optval) {
			u->debug_after.level = atoi(optval);
			char *playouts = strchr(optval, ':');
			if (playouts)  u->debug_after.playouts = atoi(playouts+1);
			else           u->debug_after.playouts = 1000;
		} else {
			u->debug_after.level = 9;
			u->debug_after.playouts = 1000;
		}
	}
	else if ((!strcasecmp(optname, "banner") && optval) ||
		 (!strcasecmp(optname, "comment") && optval)) {  NEED_RESET
		/* Set message displayed at game start on kgs.
		 * Default is "Pachi %s, Have a nice game !"
		 * '%s' is replaced by Pachi version.
		 * You can use '+' instead of ' ' if you are wrestling with kgsGtp. */
		u->banner = strdup(optval);
		for (char *b = u->banner; *b; b++)
			if (*b == '+') *b = ' ';
	}
#ifdef PACHI_PLUGINS
	else if (!strcasecmp(optname, "plugin") && optval) {
		/* Load an external plugin; filename goes before the colon,
		 * extra arguments after the colon. */
		char *pluginarg = strchr(optval, ':');
		if (pluginarg)  *pluginarg++ = 0;
		plugin_load(u->plugins, optval, pluginarg);
	}
#endif

	/** UCT behavior and policies */

	else if ((!strcasecmp(optname, "policy")
		  /* Node selection policy. ucb1amaf is the
		   * default policy implementing RAVE, while
		   * ucb1 is the simple exploration/exploitation
		   * policy. Policies can take further extra
		   * options. */
		  || !strcasecmp(optname, "random_policy")) && optval) {  NEED_RESET
		  /* A policy to be used randomly with small
		   * chance instead of the default policy. */
		char *policyarg = strchr(optval, ':');
		uct_policy_t **p = !strcasecmp(optname, "policy") ? &u->policy : &u->random_policy;
		if (policyarg)
			*policyarg++ = 0;
		if      (!strcasecmp(optval, "ucb1"))      *p = policy_ucb1_init(u, policyarg);
		else if (!strcasecmp(optval, "ucb1amaf"))  *p = policy_ucb1amaf_init(u, policyarg, b);
		else    option_error("UCT: Invalid tree policy %s\n", optval);
	}
	else if (!strcasecmp(optname, "playout") && optval) {  NEED_RESET
		/* Random simulation (playout) policy.
		 * moggy is the default policy with large
		 * amount of domain-specific knowledge and
		 * heuristics. light is a simple uniformly
		 * random move selection policy. */
		char *playoutarg = strchr(optval, ':');
		if (playoutarg)
			*playoutarg++ = 0;
		if      (!strcasecmp(optval, "moggy"))  u->playout = playout_moggy_init(playoutarg, b);
		else if (!strcasecmp(optval, "light"))  u->playout = playout_light_init(playoutarg, b);
		else    option_error("UCT: Invalid playout policy %s\n", optval);
	}
	else if (!strcasecmp(optname, "prior") && optval) {  NEED_RESET
		/* Node priors policy. When expanding a node,
		 * it will seed node values heuristically
		 * (most importantly, based on playout policy
		 * opinion, but also with regard to other
		 * things). See uct/prior.c for details.
		 * Use prior=eqex=0 to disable priors. */
		u->prior = uct_prior_init(optval, b, u);
	}
	else if (!strcasecmp(optname, "mercy") && optval) {
		/* Minimal difference of black/white captures
		 * to stop playout - "Mercy Rule". Speeds up
		 * hopeless playouts at the expense of some
		 * accuracy. */
		u->mercymin = atoi(optval);
	}
	else if (!strcasecmp(optname, "gamelen") && optval) {
		/* Maximum length of single simulation
		 * in moves. */
		u->gamelen = atoi(optval);
	}
	else if (!strcasecmp(optname, "expand_p") && optval) {
		/* Expand UCT nodes after it has been
		 * visited this many times. */
		u->expand_p = atoi(optval);
	}
	else if (!strcasecmp(optname, "random_policy_chance") && optval) {
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
	}
	else if (!strcasecmp(optname, "playout_amaf")) {
		/* Whether to include random playout moves in
		 * AMAF as well. (Otherwise, only tree moves
		 * are included in AMAF. Of course makes sense
		 * only in connection with an AMAF policy.) */
		/* with-without: 55.5% (+-4.1) */
		if (optval && *optval == '0')  u->playout_amaf = false;
		else                           u->playout_amaf = true;
	}
	else if (!strcasecmp(optname, "playout_amaf_cutoff") && optval) {
		/* Keep only first N% of playout stage AMAF
		 * information. */
		u->playout_amaf_cutoff = atoi(optval);
	}
	else if (!strcasecmp(optname, "amaf_prior") && optval) {
		/* In node policy, consider prior values
		 * part of the real result term or part
		 * of the AMAF term? */
		u->amaf_prior = atoi(optval);
	}

	/** Performance and memory management */

	else if (!strcasecmp(optname, "threads") && optval) {
		/* Default: 1 thread per core. */
		u->threads = atoi(optval);
	}
	else if (!strcasecmp(optname, "thread_model") && optval) {
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
			option_error("UCT: Invalid thread model %s\n", optval);
	}
	else if (!strcasecmp(optname, "virtual_loss") && optval) {
		/* Number of virtual losses added before evaluating a node. */
		u->virtual_loss = atoi(optval);
	}
	else if (!strcasecmp(optname, "auto_alloc")) {  NEED_RESET
	        /* Automatically grow tree memory during search (default)
		 * If tree memory runs out will allocate bigger space and resume
		 * search, so you don't have to worry about setting tree size
		 * to a big enough value ahead of time.
		 * see also "tree_size", "max_tree_size" which control initial
		 * size / max size tree can grow to. */
		u->auto_alloc = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "fixed_mem") && !optval) {  NEED_RESET
		/* Don't grow tree memory during search  (same as "auto_alloc=0")
		 * Search will stop if allocated memory runs out. Set "tree_size"
		 * or "max_tree_size" to control how much memory is allocated. */
		u->auto_alloc = false;
	}
	else if (!strcasecmp(optname, "max_mem") && optval) {  NEED_RESET
		/* Maximum amount of memory [MiB] used
		 * Default: Unlimited
		 * By default tree memory grows automatically, use this to limit
		 * global memory usage when using long thinking times (unlike
		 * "max_tree_size" takes temp tree into account when reallocating). */
		u->max_mem = (size_t)atoll(optval) * 1048576;  /* long is 4 bytes on windows! */
	}
	else if (!strcasecmp(optname, "tree_size") && optval) {  NEED_RESET
		/* Initial amount of memory [MiB] allocated for tree search.
		 * Default: 100 Mb (32 bits),  200 Mb (64 bits)
		 * By default tree memory grows automatically as needed so
		 * you don't have to set this. Can be useful to save memory /
		 * avoid reallocations if you know how much you need. */
		u->tree_size = (size_t)atoll(optval) * 1048576;  /* long is 4 bytes on windows! */
	}
	else if (!strcasecmp(optname, "max_tree_size") && optval) {  NEED_RESET
		/* Maximum amount of memory [MiB] consumed by the move tree.
		 * Default: Unlimited
		 * By default tree memory grows automatically, use this to limit
		 * memory usage when using long thinking times. When reallocating
		 * tree Pachi can temporarily use more memory, see "max_mem" to
		 * limit global memory usage instead. */
		u->max_tree_size_opt = (size_t)atoll(optval) * 1048576;  /* long is 4 bytes on windows! */
	}
	else if (!strcasecmp(optname, "reset_tree")) {
		/* Reset tree before each genmove ?
		 * Default is to reuse previous tree when not using dcnn. 
		 * When using dcnn tree is always reset (unless pondering). */
		u->genmove_reset_tree = !optval || atoi(optval);
	}

	/* Pondering */

	else if (!strcasecmp(optname, "pondering")) {
		/* Keep searching even during opponent's turn. */
		u->pondering_opt = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "dcnn_pondering_prior") && optval) {
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
	}
	else if (!strcasecmp(optname, "dcnn_pondering_mcts") && optval) {
		/* Dcnn pondering: mcts guesses for next move.
		 * Same as dcnn_pondering_prior but number of guesses picked from
		 * opponent best moves in genmove search.
		 * Default is 3. */
		size_t n = u->dcnn_pondering_mcts = atoi(optval);
		assert(n <= sizeof(u->dcnn_pondering_mcts_c) / sizeof(u->dcnn_pondering_mcts_c[0]));
	}

	/** Time control */

	else if (!strcasecmp(optname, "best2_ratio") && optval) {
		/* If set, prolong simulating while
		 * first_best/second_best playouts ratio
		 * is less than best2_ratio. */
		u->best2_ratio = atof(optval);
	}
	else if (!strcasecmp(optname, "bestr_ratio") && optval) {
		/* If set, prolong simulating while
		 * best,best_best_child values delta
		 * is more than bestr_ratio. */
		u->bestr_ratio = atof(optval);
	}
	else if (!strcasecmp(optname, "max_maintime_ratio") && optval) {
		/* If set and while not in byoyomi, prolong simulating no more than
		 * max_maintime_ratio times the normal desired thinking time. */
		u->max_maintime_ratio = atof(optval);
	}
	else if (!strcasecmp(optname, "fuseki_end") && optval) {
		/* At the very beginning it's not worth thinking
		 * too long because the playout evaluations are
		 * very noisy. So gradually increase the thinking
		 * time up to maximum when fuseki_end percent
		 * of the board has been played.
		 * This only applies if we are not in byoyomi. */
		u->fuseki_end = atoi(optval);
	}
	else if (!strcasecmp(optname, "yose_start") && optval) {
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
	}

	/** Dynamic komi */

	else if (!strcasecmp(optname, "dynkomi") && optval) {  NEED_RESET
		/* Dynamic komi approach; there are multiple
		 * ways to adjust komi dynamically throughout
		 * play. We currently support two: */
		char *dynkomiarg = strchr(optval, ':');
		if (dynkomiarg)
			*dynkomiarg++ = 0;
		if (!strcasecmp(optval, "none")) {
			u->dynkomi = uct_dynkomi_init_none(u, dynkomiarg, b);
		} else if (!strcasecmp(optval, "linear")) {
			/* You should set dynkomi_mask=1 or a very low handicap_value for white. */
			u->dynkomi = uct_dynkomi_init_linear(u, dynkomiarg, b);
		} else if (!strcasecmp(optval, "adaptive")) {
			/* There are many more knobs to crank - see uct/dynkomi.c. */
			u->dynkomi = uct_dynkomi_init_adaptive(u, dynkomiarg, b);
		} else
			option_error("UCT: Invalid dynkomi mode %s\n", optval);
	}
	else if (!strcasecmp(optname, "dynkomi_mask") && optval) {
		/* Bitmask of colors the player must be
		 * for dynkomi be applied; the default dynkomi_mask=3 allows
		 * dynkomi even in games where Pachi is white. */
		u->dynkomi_mask = atoi(optval);
	}
	else if (!strcasecmp(optname, "dynkomi_interval") && optval) {
		/* If non-zero, re-adjust dynamic komi
		 * throughout a single genmove reading,
		 * roughly every N simulations. */
		/* XXX: Does not work with tree
		 * parallelization. */
		u->dynkomi_interval = atoi(optval);
	}
	else if (!strcasecmp(optname, "extra_komi") && optval) {
		/* Initial dynamic komi settings. This
		 * is useful for the adaptive dynkomi
		 * policy as the value to start with
		 * (this is NOT kept fixed) in case
		 * there is not enough time in the search
		 * to adjust the value properly (e.g. the
		 * game was interrupted). */
		u->initial_extra_komi = atof(optval);
	}

	/** Node value result scaling */

	else if (!strcasecmp(optname, "val_scale") && optval) {
		/* How much of the game result value should be
		 * influenced by win size. Zero means it isn't. */
		u->val_scale = atof(optval);
	}
	else if (!strcasecmp(optname, "val_points") && optval) {
		/* Maximum size of win to be scaled into game
		 * result value. Zero means boardsize^2. */
		u->val_points = atoi(optval) * 2; // result values are doubled
	}
	else if (!strcasecmp(optname, "val_extra")) {
		/* If false, the score coefficient will be simply
		 * added to the value, instead of scaling the result
		 * coefficient because of it. */
		u->val_extra = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "val_byavg")) {
		/* If true, the score included in the value will
		 * be relative to average score in the current
		 * search episode inst. of jigo. */
		u->val_byavg = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "val_bytemp")) {
		/* If true, the value scaling coefficient
		 * is different based on value extremity
		 * (dist. from 0.5), linear between
		 * val_bytemp_min, val_scale. */
		u->val_bytemp = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "val_bytemp_min") && optval) {
		/* Minimum val_scale in case of val_bytemp. */
		u->val_bytemp_min = atof(optval);
	}

	/** Other heuristics */
	
	else if (!strcasecmp(optname, "patterns")) {  NEED_RESET
		/* Load pattern database. Various modules
		 * (priors, policies etc.) may make use
		 * of this database. They will request
		 * it automatically in that case, but you
		 * can use this option to tweak the pattern
		 * parameters. */
		patterns_init(&u->pc, optval, false, true);
	}
	else if (!strcasecmp(optname, "significant_threshold") && optval) {
		/* Some heuristics (XXX: none in mainline) rely
		 * on the knowledge of the last "significant"
		 * node in the descent. Such a node is
		 * considered reasonably trustworthy to carry
		 * some meaningful information in the values
		 * of the node and its children. */
		u->significant_threshold = atoi(optval);
	}

	/** Distributed engine slaves setup */
	
#ifdef DISTRIBUTED
	else if (!strcasecmp(optname, "slave")) {
		/* Act as slave for the distributed engine. */
		u->slave = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "slave_index") && optval) {
		/* Optional index if per-slave behavior is desired.
		 * Must be given as index/max */
		u->slave_index = atoi(optval);
		char *p = strchr(optval, '/');
		if (p) u->max_slaves = atoi(++p);
	}
	else if (!strcasecmp(optname, "shared_nodes") && optval) {
		/* Share at most shared_nodes between master and slave at each genmoves.
		 * Must use the same value in master and slaves. */
		u->shared_nodes = atoi(optval);
	}
	else if (!strcasecmp(optname, "shared_levels") && optval) {
		/* Share only nodes of level <= shared_levels. */
		u->shared_levels = atoi(optval);
	}
	else if (!strcasecmp(optname, "stats_hbits") && optval) {
		/* Set hash table size to 2^stats_hbits for the shared stats. */
		u->stats_hbits = atoi(optval);
	}
	else if (!strcasecmp(optname, "stats_delay") && optval) {
		/* How long to wait in slave for initial stats to build up before
		 * replying to the genmoves command (in ms) */
		u->stats_delay = 0.001 * atof(optval);
	}
#endif /* DISTRIBUTED */

	/** Presets */

	else if (!strcasecmp(optname, "maximize_score")) {  NEED_RESET
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
	}
	else
		option_error("uct: Invalid engine argument %s or missing value\n", optname);

	return true;  /* successful */
}

uct_t *
uct_state_init(engine_t *e, board_t *b)
{
	options_t *options = &e->options;
	uct_t *u = calloc2(1, uct_t);
	e->data = u;

	bool pat_setup = false;	

	u->debug_level = debug_level;
	u->reporting = u->reporting_opt = UR_TEXT;
	u->reportfreq_playouts = 1000;
	u->report_fh = stderr;
	u->gamelen = MC_GAMELEN;
	u->resign_threshold = 0.2;
	u->sure_win_threshold = 0.95;
	u->mercymin = 0;
	u->significant_threshold = 50;
	u->expand_p = 8;
	u->dumpthres = 0.01;
	u->playout_amaf = true;
	u->amaf_prior = false;
	u->auto_alloc = true;
	u->tree_size = uct_default_tree_size();
	u->max_tree_size_opt = 0;   /* unlimited */
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

#ifdef DISTRIBUTED
	u->max_slaves = -1;
	u->slave_index = -1;
	u->stats_delay = 0.01; // 10 ms
	u->shared_levels = 1;
#endif

#ifdef PACHI_PLUGINS
	u->plugins = pluginset_init(b);
#endif
	
	/* Process engine options. */
	for (int i = 0; i < options->n; i++) {
		char *err;
		if (!engine_setoption(e, b, &options->o[i], &err, true, NULL))
			die("%s", err);
		if (!strcmp(options->o[i].name, "patterns"))
			pat_setup = true;
	}
	
	if (!u->policy)
		u->policy = policy_ucb1amaf_init(u, NULL, b);

	if (!!u->random_policy_chance ^ !!u->random_policy)
		die("uct: Only one of random_policy and random_policy_chance is set\n");

	uct_tree_size_init(u, u->tree_size);

	dcnn_init(b);
	if (!using_dcnn(b))		joseki_load(board_rsize(b));
	if (!pat_setup)			patterns_init(&u->pc, NULL, false, true);
	log_nthreads(u);
	if (!u->prior)			u->prior = uct_prior_init(NULL, b, u);
	if (!u->playout)		u->playout = playout_moggy_init(NULL, b);
	if (!u->playout->debug_level)	u->playout->debug_level = u->debug_level;
#ifdef DISTRIBUTED
	if (u->slave)			uct_slave_init(u, b);
#endif
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
uct_engine_init(engine_t *e, board_t *b)
{
	e->name = "UCT";
	e->setoption = uct_setoption;
	e->board_print = uct_board_print;
	e->notify_play = uct_notify_play;
	e->chat = uct_chat;
	e->result = uct_result;
	e->genmove = uct_genmove;
	e->genmove_analyze = uct_genmove_analyze;
	e->best_moves = uct_best_moves;
	e->evaluate = uct_evaluate;
	e->analyze = uct_analyze;
	e->dead_groups = uct_dead_groups;
	e->stop = uct_stop;
	e->done = uct_done;
	e->ownermap = uct_ownermap;

	uct_t *u = uct_state_init(e, b);

#ifdef DISTRIBUTED
	e->genmoves = uct_genmoves;
	if (u->slave)
		e->notify = uct_notify;
#endif

	e->comment = u->banner;
}
