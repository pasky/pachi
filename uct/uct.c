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
#include "move.h"
#include "mq.h"
#include "playout.h"
#include "playout/elo.h"
#include "playout/moggy.h"
#include "playout/light.h"
#include "tactics.h"
#include "timeinfo.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/search.h"
#include "uct/slave.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"

struct uct_policy *policy_ucb1_init(struct uct *u, char *arg);
struct uct_policy *policy_ucb1amaf_init(struct uct *u, char *arg);
static void uct_pondering_start(struct uct *u, struct board *b0, struct tree *t, enum stone color);

/* Maximal simulation length. */
#define MC_GAMELEN	MAX_GAMELEN


static void
setup_state(struct uct *u, struct board *b, enum stone color)
{
	u->t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0,
			 u->local_tree_aging, u->stats_hbits);
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
	if (u->t->use_extra_komi && !u->pondering && u->dynkomi->permove)
		u->t->extra_komi = u->dynkomi->permove(u->dynkomi, b, u->t);
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
	dead_group_list(u, b, &mq);
	if (pass_all_alive && mq.moves > 0)
		return false; // We need to remove some dead groups first.
	return pass_is_safe(b, color, &mq);
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



/* Run time-limited MCTS search on foreground. */
static int
uct_search(struct uct *u, struct board *b, struct time_info *ti, enum stone color, struct tree *t)
{
	struct uct_search_state s;
	uct_search_start(u, b, color, t, ti, &s);
	if (UDEBUGL(2) && s.base_playouts > 0)
		fprintf(stderr, "<pre-simulated %d games>\n", s.base_playouts);

	/* The search tree is ctx->t. This is normally == t, but in case of
	 * TM_ROOT, it is one of the trees belonging to the independent
	 * workers. It is important to reference ctx->t directly since the
	 * thread manager will swap the tree pointer asynchronously. */
	/* XXX: This means TM_ROOT support is suboptimal since single stalled
	 * thread can stall the others in case of limiting the search by game
	 * count. However, TM_ROOT just does not deserve any more extra code
	 * right now. */

	/* Now, just periodically poll the search tree. */
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
		fprintf(stderr, "(avg score %f/%d value %f/%d)\n",
			u->dynkomi->score.value, u->dynkomi->score.playouts,
			u->dynkomi->value.value, u->dynkomi->value.playouts);
	if (UDEBUGL(0))
		uct_progress_status(u, t, color, ctx->games);

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
	struct move m = { t->root->coord, t->root_color };
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
		uct_progress_status(u, ctx->t, ctx->color, ctx->games);
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

static coord_t *
uct_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	double start_time = time_now();
	struct uct *u = e->data;
	uct_pondering_stop(u);
	uct_genmove_setup(u, b, color);

        /* Start the Monte Carlo Tree Search! */
	int base_playouts = u->t->root->u.playouts;
	int played_games = uct_search(u, b, ti, color, u->t);

	coord_t best_coord;
	struct tree_node *best;
	best = uct_search_result(u, b, color, pass_all_alive, played_games, base_playouts, &best_coord);

	if (UDEBUGL(2)) {
		double time = time_now() - start_time + 0.000001; /* avoid divide by zero */
		fprintf(stderr, "genmove in %0.2fs (%d games/s, %d games/s/thread)\n",
			time, (int)(played_games/time), (int)(played_games/time/u->threads));
	}

	if (!best) {
		/* Pass or resign. */
		reset_state(u);
		return coord_copy(best_coord);
	}
	tree_promote_node(u->t, &best);

	/* After a pass, pondering is harmful for two reasons:
	 * (i) We might keep pondering even when the game is over.
	 * Of course this is the case for opponent resign as well.
	 * (ii) More importantly, the ownermap will get skewed since
	 * the UCT will start cutting off any playouts. */
	if (u->pondering_opt && !is_pass(best->coord)) {
		uct_pondering_start(u, b, u->t, stone_other(color));
	}
	return coord_copy(best_coord);
}


bool
uct_genbook(struct engine *e, struct board *b, struct time_info *ti, enum stone color)
{
	struct uct *u = e->data;
	if (!u->t) uct_prepare_move(u, b, color);
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
	struct tree *t = tree_init(b, color, u->fast_alloc ? u->max_tree_size : 0, u->local_tree_aging, 0);
	tree_load(t, b);
	tree_dump(t, 0);
	tree_done(t);
}


struct uct *
uct_state_init(char *arg, struct board *b)
{
	struct uct *u = calloc2(1, sizeof(struct uct));
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
	/* TODO: Adjust this by number of simulations - it's more important how
	 * many times per move we do the adjustment. */
	u->dynkomi_interval = 500;

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
				/* XXX: Does not work with tree
				 * parallelization. */
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
				/* Whether to consider passing only after all
				 * dead groups were removed from the board;
				 * this is like all genmoves are in fact
				 * kgs-genmove_cleanup. */
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
	if (u->slave && !u->parallel_tree) {
		/* node->pu used by slave. */
		fprintf(stderr, "slave not supported with root parallelization.\n");
		exit(1);
	}
	if (u->fast_alloc)
		u->max_tree_size = (100ULL * u->max_tree_size) / (100 + MIN_FREE_MEM_PERCENT);

	if (!u->prior)
		u->prior = uct_prior_init(NULL, b);

	if (!u->playout)
		u->playout = playout_moggy_init(NULL, b);
	u->playout->debug_level = u->debug_level;

	u->ownermap.map = malloc2(board_size2(b) * sizeof(u->ownermap.map[0]));

	if (u->slave) {
		if (!u->stats_hbits) u->stats_hbits = DEFAULT_STATS_HBITS;
		if (!u->shared_nodes) u->shared_nodes = DEFAULT_SHARED_NODES;
		if (!u->shared_levels) u->shared_levels = 1;
		assert(u->shared_levels * board_bits2(b) <= 8 * sizeof(path_t));
	}

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
	struct engine *e = calloc2(1, sizeof(struct engine));
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
	e->comment = malloc2(sizeof(banner) + strlen(u->banner) + 1);
	sprintf(e->comment, "%s %s", banner, u->banner);

	return e;
}
