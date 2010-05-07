#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VERBOSE_LOGS 1000
#define DEBUG

#include "debug.h"
#include "board.h"
#include "gtp.h"
#include "move.h"
#include "timeinfo.h"
#include "distributed/distributed.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/slave.h"
#include "uct/tree.h"


/* UCT infrastructure for a distributed engine slave. */

enum parse_code
uct_notify(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply)
{
	struct uct *u = e->data;

	static bool board_resized = false;
	if (is_gamestart(cmd)) {
		board_resized = true;
		uct_pondering_stop(u);
	}

	/* Force resending the whole command history if we are out of sync
	 * but do it only once, not if already getting the history. */
	if ((move_number(id) != b->moves || !board_resized)
	    && !reply_disabled(id) && !is_reset(cmd)) {
		if (UDEBUGL(0))
			fprintf(stderr, "Out of sync, id %d, move %d\n", id, b->moves);

		/* Skip rest of multi-line command (genmoves only) */
		if (!strcasecmp(cmd, "pachi-genmoves")
		    || !strcasecmp(cmd, "pachi-genmoves_cleanup")) {
			char line[128];
			while (fgets(line, sizeof(line), stdin) && *line != '\n') ;
		}

		static char buf[128];
		snprintf(buf, sizeof(buf), "out of sync, move %d expected", b->moves);
		*reply = buf;
		return P_DONE_ERROR;
	}
	return reply_disabled(id) ? P_NOREPLY : P_OK;
}


/* Read the move stats sent by the master, as a binary array of
 * incr_stats structs. The stats come sorted by increasing coord path.
 * To simplify the code, we assume that master and slave have the same
 * architecture (store values identically).
 * Keep this code in sync with distributed/distributed.c:select_best_move().
 * Return true if ok, false if error. */
static bool
receive_stats(struct uct *u, int size)
{
	if (size % sizeof(struct incr_stats)) return false;
	int nodes = size / sizeof(struct incr_stats);

	assert(nodes);
	double start_time = time_now();

	for (int n = 0; n < nodes; n++) {
		struct incr_stats is;
		if (fread(&is, sizeof(struct incr_stats), 1, stdin) != 1)
			return false;

		if (UDEBUGL(7))
			fprintf(stderr, "read %5d/%d %6d %.3f %"PRIpath"\n", n, nodes,
				is.incr.playouts, is.incr.value, is.coord_path);

		/* TODO: update the stats in the tree. */
	}
	if (DEBUGVV(2))
		fprintf(stderr, "read args for %d nodes in %.4fms\n", nodes,
			(time_now() - start_time)*1000);
	return true;
}

/* Get stats updates for the distributed engine. Return a buffer with
 * one line "played_own root_playouts threads keep_looking" then a list
 * of lines "coord playouts value amaf_playouts amaf_value".
 * The last line must not end with \n.
 * If c is pass or resign, add this move with root->playouts weight.
 * This function is called only by the main thread, but may be
 * called while the tree is updated by the worker threads.
 * Keep this code in sync with select_best_move(). */
static char *
report_stats(struct uct *u, struct board *b, coord_t c, bool keep_looking)
{
	static char reply[10240];
	char *r = reply;
	char *end = reply + sizeof(reply);
	struct tree_node *root = u->t->root;
	r += snprintf(r, end - r, "%d %d %d %d", u->played_own, root->u.playouts, u->threads, keep_looking);
	int min_playouts = root->u.playouts / 100;

	/* Give a large weight to pass or resign, but still allow other moves.
	 * Only root->u.playouts will be used (majority vote) but send amaf
	 * stats too for consistency. */
	if (is_pass(c) || is_resign(c))
		r += snprintf(r, end - r, "\n%s %d %.1f %d %.1f", coord2sstr(c, b),
			      root->u.playouts, 0.0, root->amaf.playouts, 0.0);

	/* We rely on the fact that root->children is set only
	 * after all children are created. */
	for (struct tree_node *ni = root->children; ni; ni = ni->sibling) {

		if (is_pass(ni->coord)) continue;
		struct node_stats *ns = &u->stats[ni->coord];
		ns->last_sent_own.u.playouts = ns->last_sent_own.amaf.playouts = 0;
		ns->node = ni;
		if (ni->u.playouts <= min_playouts || ni->hints & TREE_HINT_INVALID)
			continue;

		char *coord = coord2sstr(ni->coord, b);
		/* We return the values as stored in the tree, so from black's view.
		 *   own = total_in_tree - added_from_others */
		struct move_stats2 s = { .u = ni->u, .amaf = ni->amaf };
		struct move_stats2 others = ns->added_from_others;
		if (s.u.playouts - others.u.playouts <= min_playouts)
			continue;
		if (others.u.playouts)
			stats_rm_result(&s.u, others.u.value, others.u.playouts);
		if (others.amaf.playouts)
			stats_rm_result(&s.amaf, others.amaf.value, others.amaf.playouts);

		r += snprintf(r, end - r, "\n%s %d %.7f %d %.7f", coord,
			      s.u.playouts, s.u.value, s.amaf.playouts, s.amaf.value);
		ns->last_sent_own = s;
		/* If the master discards these values because this slave
		 * is out of sync, u->stats will be reset anyway. */
	}
	return reply;
}

/* How long to wait in slave for initial stats to build up before
 * replying to the genmoves command (in seconds) */
#define MIN_STATS_INTERVAL 0.05 /* 50ms */

/* genmoves is issued by the distributed engine master to all slaves, to:
 * 1. Start a MCTS search if not running yet
 * 2. Report current move statistics of the on-going search.
 * The MCTS search is left running on the background when uct_genmoves()
 * returns. It is stopped by receiving a play GTP command, triggering
 * uct_pondering_stop(). */
/* genmoves gets in the args parameter
 * "played_games nodes main_time byoyomi_time byoyomi_periods byoyomi_stones @size"
 * and reads a binary array of coord, playouts, value to get stats of other slaves,
 * except possibly for the first call at a given move number.
 * See report_stats() for the description of the return value. */
char *
uct_genmoves(struct engine *e, struct board *b, struct time_info *ti, enum stone color,
	     char *args, bool pass_all_alive)
{
	struct uct *u = e->data;
	assert(u->slave);

	/* Prepare the state if the search is not already running.
	 * We must do this first since we tweak the state below
	 * based on instructions from the master. */
	if (!thread_manager_running)
		uct_genmove_setup(u, b, color);

	/* Get playouts and time information from master. Keep this code
	 * in sync with distibuted/distributed.c:distributed_genmove(). */
	if ((ti->dim == TD_WALLTIME
	     && sscanf(args, "%d %lf %lf %d %d", &u->played_all,
		       &ti->len.t.main_time, &ti->len.t.byoyomi_time,
		       &ti->len.t.byoyomi_periods, &ti->len.t.byoyomi_stones) != 5)

	    || (ti->dim == TD_GAMES && sscanf(args, "%d", &u->played_all) != 1)) {
		return NULL;
	}

	static struct uct_search_state s;
	if (!thread_manager_running) {
		/* This is the first genmoves issue, start the MCTS
		 * now and let it run while we receive stats. */
		memset(&s, 0, sizeof(s));
		uct_search_start(u, b, color, u->t, ti, &s);
	}

	/* Read binary incremental stats if present, otherwise
	 * wait a bit to populate the statistics. */
	int size = 0;
	char *sizep = strchr(args, '@');
	if (sizep) size = atoi(sizep+1);
	if (!size) {
		time_sleep(MIN_STATS_INTERVAL);
	} else if (!receive_stats(u, size)) {
		return NULL;
	}

	/* Check the state of the Monte Carlo Tree Search. */

	int played_games = uct_search_games(&s);
	uct_search_progress(u, b, color, u->t, ti, &s, played_games);
	u->played_own = played_games - s.base_playouts;

	bool keep_looking = !uct_search_check_stop(u, b, color, u->t, ti, &s, played_games);
	coord_t best_coord;
	uct_search_result(u, b, color, pass_all_alive, played_games, s.base_playouts, &best_coord);

	char *reply = report_stats(u, b, best_coord, keep_looking);
	return reply;
}
