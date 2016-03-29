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
#include "dcnn.h"
#include "joseki/base.h"
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

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg, struct board *board);
static void uct_pondering_start(struct uct *u, struct board *b0, struct tree *t, enum stone color);

/* Maximal simulation length. */
#define MC_GAMELEN	MAX_GAMELEN


static void
setup_state(struct uct *u, struct board *b, enum stone color)
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
reset_state(struct uct *u)
{
	assert(u->t);
	tree_done(u->t); u->t = NULL;
}

static void
setup_dynkomi(struct uct *u, struct board *b, enum stone to_play)
{
	if (u->t->use_extra_komi && !u->pondering && u->dynkomi->permove)
		u->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, u->t);
	else if (!u->t->use_extra_komi)
		u->t->extra_komi = 0;
}

void
uct_prepare_move(struct uct *u, struct board *b, enum stone color)
{
	if (u->t) {
		/* Verify that we have sane state. */
		assert(b->es == u);
		assert(u->t && b->moves);
		if (color != stone_other(u->t->root_color)) {
			fprintf(stderr, "Fatal: Non-alternating play detected %d %d\n",
				color, u->t->root_color);
			exit(1);
		}
		uct_htable_reset(u->t);

	} else {
		/* We need fresh state. */
		b->es = u;
		setup_state(u, b, color);
	}

	u->ownermap.playouts = 0;
	memset(u->ownermap.map, 0, board_size2(b) * sizeof(u->ownermap.map[0]));
	u->played_own = u->played_all = 0;
}

static void
dead_group_list(struct uct *u, struct board *b, struct move_queue *mq)
{
	enum gj_state gs_array[board_size2(b)];
	struct group_judgement gj = { .thres = GJ_THRES, .gs = gs_array };
	board_ownermap_judge_groups(b, &u->ownermap, &gj);
	groups_of_status(b, &gj, GS_DEAD, mq);
}

bool
uct_pass_is_safe(struct uct *u, struct board *b, enum stone color, bool pass_all_alive)
{
	/* Make sure enough playouts are simulated to get a reasonable dead group list. */
	while (u->ownermap.playouts < GJ_MINGAMES)
		uct_playout(u, b, color, u->t);

	struct move_queue mq = { .moves = 0 };
	dead_group_list(u, b, &mq);
	if (pass_all_alive) {
		for (unsigned int i = 0; i < mq.moves; i++) {
			if (board_at(b, mq.move[i]) == stone_other(color)) {
				return false; // We need to remove opponent dead groups first.
			}
		}
		mq.moves = 0; // our dead stones are alive when pass_all_alive is true
	}
	if (u->allow_losing_pass) {
		foreach_point(b) {
			if (board_at(b, c) == S_OFFBOARD)
				continue;
			if (board_ownermap_judge_point(&u->ownermap, c, GJ_THRES) == PJ_UNKNOWN) {
				if (UDEBUGL(3))
					fprintf(stderr, "uct_pass_is_safe fails at %s[%d]\n", coord2sstr(c, b), c);
				return false; // Unclear point, clarify first.
			}
		} foreach_point_end;
		return true;
	}
	return pass_is_safe(b, color, &mq);
}

static char *
uct_printhook_ownermap(struct board *board, coord_t c, char *s, char *end)
{
	struct uct *u = board->es;
	if (!u) {
		strcat(s, ". ");
		return s + 2;
	}
	const char chr[] = ":XO,"; // dame, black, white, unclear
	const char chm[] = ":xo,";
	char ch = chr[board_ownermap_judge_point(&u->ownermap, c, GJ_THRES)];
	if (ch == ',') { // less precise estimate then?
		ch = chm[board_ownermap_judge_point(&u->ownermap, c, 0.67)];
	}
	s += snprintf(s, end - s, "%c ", ch);
	return s;
}

static float
uct_owner_map(struct engine *e, struct board *b, coord_t c)
{
	struct uct *u = b->es;
	return board_ownermap_estimate_point(&u->ownermap, c);
}

static char *
uct_notify_play(struct engine *e, struct board *b, struct move *m, char *enginearg)
{
	struct uct *u = e->data;
	if (!u->t) {
		/* No state, create one - this is probably game beginning
		 * and we need to load the opening tbook right now. */
		uct_prepare_move(u, b, m->color);
		assert(u->t);
	}

	/* Stop pondering, required by tree_promote_at() */
	uct_pondering_stop(u);
	if (UDEBUGL(2) && u->slave)
		tree_dump(u->t, u->dumpthres);

	if (is_resign(m->coord)) {
		/* Reset state. */
		reset_state(u);
		return NULL;
	}

	/* Promote node of the appropriate move to the tree root. */
	assert(u->t->root);
	if (u->t->untrustworthy_tree | !tree_promote_at(u->t, b, m->coord)) {
		if (UDEBUGL(3)) {
			if (u->t->untrustworthy_tree)
				fprintf(stderr, "Not promoting move node in untrustworthy tree.\n");
			else
				fprintf(stderr, "Warning: Cannot promote move node! Several play commands in row?\n");
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
		uct_pondering_start(u, b, u->t, stone_other(m->color));

	return NULL;
}

static char *
uct_undo(struct engine *e, struct board *b)
{
	struct uct *u = e->data;

	if (!u->t) return NULL;
	uct_pondering_stop(u);
	u->initial_extra_komi = u->t->extra_komi;
	reset_state(u);
	return NULL;
}

static char *
uct_result(struct engine *e, struct board *b)
{
	struct uct *u = e->data;
	static char reply[1024];

	if (!u->t)
		return NULL;
	enum stone color = u->t->root_color;
	struct tree_node *n = u->t->root;
	snprintf(reply, 1024, "%s %s %d %.2f %.1f",
		 stone2str(color), coord2sstr(node_coord(n), b),
		 n->u.playouts, tree_node_get_value(u->t, -1, n->u.value),
		 u->t->use_extra_komi ? u->t->extra_komi : 0);
	return reply;
}

static char *
uct_chat(struct engine *e, struct board *b, bool opponent, char *from, char *cmd)
{
	struct uct *u = e->data;

	if (!u->t)
		return generic_chat(b, opponent, from, cmd, S_NONE, pass, 0, 1, u->threads, 0.0, 0.0);

	struct tree_node *n = u->t->root;
	double winrate = tree_node_get_value(u->t, -1, n->u.value);
	double extra_komi = u->t->use_extra_komi && fabs(u->t->extra_komi) >= 0.5 ? u->t->extra_komi : 0;

	return generic_chat(b, opponent, from, cmd, u->t->root_color, node_coord(n), n->u.playouts, 1,
			    u->threads, winrate, extra_komi);
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
		uct_prepare_move(u, b, S_BLACK); assert(u->t);
		mock_state = true;
	}
	/* Make sure the ownermap is well-seeded. */
	while (u->ownermap.playouts < GJ_MINGAMES)
		uct_playout(u, b, S_BLACK, u->t);
	/* Show the ownermap: */
	if (DEBUGL(2))
		board_print_custom(b, stderr, uct_printhook_ownermap);

	dead_group_list(u, b, mq);

	if (mock_state) {
		/* Clean up the mock state in case we will receive
		 * a genmove; we could get a non-alternating-move
		 * error from uct_prepare_move() in that case otherwise. */
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
uct_stop(struct engine *e)
{
	/* This is called on game over notification. However, an undo
	 * and game resume can follow, so don't panic yet and just
	 * relax and stop thinking so that we don't waste CPU. */
	struct uct *u = e->data;
	uct_pondering_stop(u);
}

static void
uct_done(struct engine *e)
{
	/* This is called on engine reset, especially when clear_board
	 * is received and new game should begin. */
	free(e->comment);

	struct uct *u = e->data;
	uct_pondering_stop(u);
	if (u->t) reset_state(u);
	if (u->dynkomi) u->dynkomi->done(u->dynkomi);
	free(u->ownermap.map);

	if (u->policy) u->policy->done(u->policy);
	if (u->random_policy) u->random_policy->done(u->random_policy);
	playout_policy_done(u->playout);
	uct_prior_done(u->prior);
	joseki_done(u->jdict);
	pluginset_done(u->plugins);
}



/* Run time-limited MCTS search on foreground. */
static int
uct_search(struct uct *u, struct board *b, struct time_info *ti, enum stone color, struct tree *t, bool print_progress)
{
	struct uct_search_state s;
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

	struct uct_thread_ctx *ctx = uct_search_stop();
	if (UDEBUGL(2)) tree_dump(t, u->dumpthres);
	if (UDEBUGL(2))
		fprintf(stderr, "(avg score %f/%d; dynkomi's %f/%d value %f/%d)\n",
			t->avg_score.value, t->avg_score.playouts,
			u->dynkomi->score.value, u->dynkomi->score.playouts,
			u->dynkomi->value.value, u->dynkomi->value.playouts);
	if (print_progress)
		uct_progress_status(u, t, color, ctx->games, NULL);

	if (u->debug_after.playouts > 0) {
		/* Now, start an additional run of playouts, single threaded. */
		struct time_info debug_ti = {
			.period = TT_MOVE,
			.dim = TD_GAMES,
		};
		debug_ti.len.games = t->root->u.playouts + u->debug_after.playouts;

		board_print_custom(b, stderr, uct_printhook_ownermap);
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

/* Start pondering background with @color to play. */
static void
uct_pondering_start(struct uct *u, struct board *b0, struct tree *t, enum stone color)
{
	if (UDEBUGL(1))
		fprintf(stderr, "Starting to ponder with color %s\n", stone2str(stone_other(color)));
	u->pondering = true;

	/* We need a local board copy to ponder upon. */
	struct board *b = malloc2(sizeof(*b)); board_copy(b, b0);

	/* *b0 did not have the genmove'd move played yet. */
	struct move m = { node_coord(t->root), t->root_color };
	int res = board_play(b, &m);
	assert(res >= 0);
	setup_dynkomi(u, b, stone_other(m.color));

	/* Start MCTS manager thread "headless". */
	static struct uct_search_state s;
	uct_search_start(u, b, color, t, NULL, &s);
}

/* uct_search_stop() frontend for the pondering (non-genmove) mode, and
 * to stop the background search for a slave in the distributed engine. */
void
uct_pondering_stop(struct uct *u)
{
	if (!thread_manager_running)
		return;

	/* Stop the thread manager. */
	struct uct_thread_ctx *ctx = uct_search_stop();
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
uct_genmove_setup(struct uct *u, struct board *b, enum stone color)
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
}

static void
uct_live_gfx_hook(struct engine *e)
{
	struct uct *u = e->data;
	/* Hack: Override reportfreq to get decent update rates in GoGui */
	u->reportfreq = 1000;
}

/* Kindof like uct_genmove() but just find the best candidates */
static void
uct_best_moves(struct engine *e, struct board *b, enum stone color)
{
	struct time_info ti = { .period = TT_NULL };
	double start_time = time_now();
	struct uct *u = e->data;
	uct_pondering_stop(u);
	if (u->t)
		reset_state(u);
	uct_genmove_setup(u, b, color);

        /* Start the Monte Carlo Tree Search! */
	int base_playouts = u->t->root->u.playouts;
	int played_games = uct_search(u, b, &ti, color, u->t, false);

	coord_t best_coord;
	uct_search_result(u, b, color, u->pass_all_alive, played_games, base_playouts, &best_coord);

	if (UDEBUGL(2)) {
		double time = time_now() - start_time + 0.000001; /* avoid divide by zero */
		fprintf(stderr, "genmove in %0.2fs (%d games/s, %d games/s/thread)\n",
			time, (int)(played_games/time), (int)(played_games/time/u->threads));
	}

	uct_progress_status(u, u->t, color, played_games, &best_coord);
	reset_state(u);
}

static coord_t *
uct_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	double start_time = time_now();
	struct uct *u = e->data;
	u->pass_all_alive |= pass_all_alive;
	uct_pondering_stop(u);

	if (using_dcnn(b)) {
		// dcnn hack: reset state to make dcnn priors kick in.
		// FIXME this makes pondering useless when using dcnn ...
		if (u->t) {
			u->initial_extra_komi = u->t->extra_komi;
			reset_state(u);
		}
	}

	uct_genmove_setup(u, b, color);

        /* Start the Monte Carlo Tree Search! */
	int base_playouts = u->t->root->u.playouts;
	int played_games = uct_search(u, b, ti, color, u->t, false);

	coord_t best_coord;
	struct tree_node *best;
	best = uct_search_result(u, b, color, u->pass_all_alive, played_games, base_playouts, &best_coord);

	if (UDEBUGL(2)) {
		double time = time_now() - start_time + 0.000001; /* avoid divide by zero */
		fprintf(stderr, "genmove in %0.2fs (%d games/s, %d games/s/thread)\n",
			time, (int)(played_games/time), (int)(played_games/time/u->threads));
	}

	uct_progress_status(u, u->t, color, played_games, &best_coord);

	if (!best) {
		/* Pass or resign. */
		if (is_pass(best_coord))
			u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
		return coord_copy(best_coord);
	}

	if (!u->t->untrustworthy_tree) {
		tree_promote_node(u->t, &best);
	} else {
		/* Throw away an untrustworthy tree. */
		/* Preserve dynamic komi information, though, that is important. */
		u->initial_extra_komi = u->t->extra_komi;
		reset_state(u);
	}

	/* After a pass, pondering is harmful for two reasons:
	 * (i) We might keep pondering even when the game is over.
	 * Of course this is the case for opponent resign as well.
	 * (ii) More importantly, the ownermap will get skewed since
	 * the UCT will start cutting off any playouts. */
	if (u->pondering_opt && u->t && !is_pass(node_coord(best))) {
		uct_pondering_start(u, b, u->t, stone_other(color));
	}
	return coord_copy(best_coord);
}


bool
uct_gentbook(struct engine *e, struct board *b, struct time_info *ti, enum stone color)
{
	struct uct *u = e->data;
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
uct_dumptbook(struct engine *e, struct board *b, enum stone color)
{
	struct uct *u = e->data;
	struct tree *t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0,
			 u->max_pruned_size, u->pruning_threshold, u->local_tree_aging, 0);
	tree_load(t, b);
	tree_dump(t, 0);
	tree_done(t);
}


floating_t
uct_evaluate_one(struct engine *e, struct board *b, struct time_info *ti, coord_t c, enum stone color)
{
	struct uct *u = e->data;

	struct board b2;
	board_copy(&b2, b);
	struct move m = { c, color };
	int res = board_play(&b2, &m);
	if (res < 0)
		return NAN;
	color = stone_other(color);

	if (u->t) reset_state(u);
	uct_prepare_move(u, &b2, color);
	assert(u->t);

	floating_t bestval;
	uct_search(u, &b2, ti, color, u->t, true);
	struct tree_node *best = u->policy->choose(u->policy, u->t->root, &b2, color, resign);
	if (!best) {
		bestval = NAN; // the opponent has no reply!
	} else {
		bestval = tree_node_get_value(u->t, 1, best->u.value);
	}

	reset_state(u); // clean our junk

	return isnan(bestval) ? NAN : 1.0f - bestval;
}

void
uct_evaluate(struct engine *e, struct board *b, struct time_info *ti, floating_t *vals, enum stone color)
{
	for (int i = 0; i < b->flen; i++) {
		if (is_pass(b->f[i]))
			vals[i] = NAN;
		else
			vals[i] = uct_evaluate_one(e, b, ti, b->f[i], color);
	}
}


struct uct *
uct_state_init(char *arg, struct board *b)
{
	struct uct *u = calloc2(1, sizeof(struct uct));
	bool pat_setup = false;

	u->debug_level = debug_level;
	u->reportfreq = 10000;
	u->gamelen = MC_GAMELEN;
	u->resign_threshold = 0.2;
	u->sure_win_threshold = 0.95;
	u->mercymin = 0;
	u->significant_threshold = 50;
	u->expand_p = 8;
	u->dumpthres = 0.01;
	u->playout_amaf = true;
	u->amaf_prior = false;
	u->max_tree_size = 1408ULL * 1048576;
	u->fast_alloc = true;
	u->pruning_threshold = 0;

	u->threads = 1;
	u->thread_model = TM_TREEVL;
	u->virtual_loss = 1;

	u->pondering_opt = true;

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
	u->dynkomi_interval = 1000;
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

	u->plugins = pluginset_init(b);

	u->jdict = joseki_load(b->size);

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
				} else {
					fprintf(stderr, "UCT: Invalid reporting format %s\n", optval);
					exit(1);
				}
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
			} else if (!strcasecmp(optname, "territory_scoring")) {
				/* Use territory scoring (default is area scoring).
				 * An explicit kgs-rules command overrides this. */
				u->territory_scoring = !optval || atoi(optval);
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
			} else if (!strcasecmp(optname, "banner") && optval) {
				/* Additional banner string. This must come as the
				 * last engine parameter. You can use '+' instead
				 * of ' ' if you are wrestling with kgsGtp. */
				if (*next) *--next = ',';
				u->banner = strdup(optval);
				for (char *b = u->banner; *b; b++) {
					if (*b == '+') *b = ' ';
				}
				break;
			} else if (!strcasecmp(optname, "plugin") && optval) {
				/* Load an external plugin; filename goes before the colon,
				 * extra arguments after the colon. */
				char *pluginarg = strchr(optval, ':');
				if (pluginarg)
					*pluginarg++ = 0;
				plugin_load(u->plugins, optval, pluginarg);

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
				struct uct_policy **p = !strcasecmp(optname, "policy") ? &u->policy : &u->random_policy;
				if (policyarg)
					*policyarg++ = 0;
				if (!strcasecmp(optval, "ucb1")) {
					*p = policy_ucb1_init(u, policyarg);
				} else if (!strcasecmp(optval, "ucb1amaf")) {
					*p = policy_ucb1amaf_init(u, policyarg, b);
				} else {
					fprintf(stderr, "UCT: Invalid tree policy %s\n", optval);
					exit(1);
				}
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
					u->playout = playout_moggy_init(playoutarg, b, u->jdict);
				} else if (!strcasecmp(optval, "light")) {
					u->playout = playout_light_init(playoutarg, b);
				} else {
					fprintf(stderr, "UCT: Invalid playout policy %s\n", optval);
					exit(1);
				}
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
				} else {
					fprintf(stderr, "UCT: Invalid thread model %s\n", optval);
					exit(1);
				}
			} else if (!strcasecmp(optname, "virtual_loss") && optval) {
				/* Number of virtual losses added before evaluating a node. */
				u->virtual_loss = atoi(optval);
			} else if (!strcasecmp(optname, "pondering")) {
				/* Keep searching even during opponent's turn. */
				u->pondering_opt = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "max_tree_size") && optval) {
				/* Maximum amount of memory [MiB] consumed by the move tree.
				 * For fast_alloc it includes the temp tree used for pruning.
				 * Default is 3072 (3 GiB). */
				u->max_tree_size = atol(optval) * 1048576;
			} else if (!strcasecmp(optname, "fast_alloc")) {
				u->fast_alloc = !optval || atoi(optval);
			} else if (!strcasecmp(optname, "pruning_threshold") && optval) {
				/* Force pruning at beginning of a move if the tree consumes
				 * more than this [MiB]. Default is 10% of max_tree_size.
				 * Increase to reduce pruning time overhead if memory is plentiful.
				 * This option is meaningful only for fast_alloc. */
				u->pruning_threshold = atol(optval) * 1048576;

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
				} else {
					fprintf(stderr, "UCT: Invalid dynkomi mode %s\n", optval);
					exit(1);
				}
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
				if (u->tenuki_d > TREE_NODE_D_MAX + 1) {
					fprintf(stderr, "uct: tenuki_d must not be larger than TREE_NODE_D_MAX+1 %d\n", TREE_NODE_D_MAX + 1);
					exit(1);
				}
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
				else {
					fprintf(stderr, "uct: unknown local_tree_eval %s\n", optval);
					exit(1);
				}
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
				patterns_init(&u->pat, optval, false, true);
				u->want_pat = pat_setup = true;
			} else if (!strcasecmp(optname, "significant_threshold") && optval) {
				/* Some heuristics (XXX: none in mainline) rely
				 * on the knowledge of the last "significant"
				 * node in the descent. Such a node is
				 * considered reasonably trustworthy to carry
				 * some meaningful information in the values
				 * of the node and its children. */
				u->significant_threshold = atoi(optval);

			/** Distributed engine slaves setup */

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

			} else {
				fprintf(stderr, "uct: Invalid engine argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	if (!u->policy)
		u->policy = policy_ucb1amaf_init(u, NULL, b);

	if (!!u->random_policy_chance ^ !!u->random_policy) {
		fprintf(stderr, "uct: Only one of random_policy and random_policy_chance is set\n");
		exit(1);
	}

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

	if (!u->prior)
		u->prior = uct_prior_init(NULL, b, u);

	if (!u->playout)
		u->playout = playout_moggy_init(NULL, b, u->jdict);
	if (!u->playout->debug_level)
		u->playout->debug_level = u->debug_level;

	if (u->want_pat && !pat_setup)
		patterns_init(&u->pat, NULL, false, true);
	dcnn_init();

	u->ownermap.map = malloc2(board_size2(b) * sizeof(u->ownermap.map[0]));

	if (u->slave) {
		if (!u->stats_hbits) u->stats_hbits = DEFAULT_STATS_HBITS;
		if (!u->shared_nodes) u->shared_nodes = DEFAULT_SHARED_NODES;
		assert(u->shared_levels * board_bits2(b) <= 8 * (int)sizeof(path_t));
	}

	if (!u->dynkomi)
		u->dynkomi = board_small(b) ? uct_dynkomi_init_none(u, NULL, b)
			: uct_dynkomi_init_linear(u, NULL, b);

	/* Some things remain uninitialized for now - the opening tbook
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
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "UCT";
	e->printhook = uct_printhook_ownermap;
	e->notify_play = uct_notify_play;
	e->chat = uct_chat;
	e->undo = uct_undo;
	e->result = uct_result;
	e->genmove = uct_genmove;
	e->genmoves = uct_genmoves;
	e->evaluate = uct_evaluate;
	e->dead_group_list = uct_dead_group_list;
	e->stop = uct_stop;
	e->done = uct_done;
	e->owner_map = uct_owner_map;
	e->best_moves = uct_best_moves;
	e->live_gfx_hook = uct_live_gfx_hook;
	e->data = u;
	if (u->slave)
		e->notify = uct_notify;

	const char banner[] = "If you believe you have won but I am still playing, "
		"please help me understand by capturing all dead stones. "
		"Anyone can send me 'winrate' in private chat to get my assessment of the position.";
	if (!u->banner) u->banner = "";
	e->comment = malloc2(sizeof(banner) + strlen(u->banner) + 1);
	sprintf(e->comment, "%s %s", banner, u->banner);

	return e;
}
