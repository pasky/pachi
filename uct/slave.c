#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "gtp.h"
#include "move.h"
#include "timeinfo.h"
#include "distributed/distributed.h"
#include "uct/internal.h"
#include "uct/slave.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"


/* UCT infrastructure for a distributed engine slave. */

enum parse_code
uct_notify(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply)
{
	struct uct *u = e->data;

	static bool board_resized = false;
	board_resized |= is_gamestart(cmd);

	/* Force resending the whole command history if we are out of sync
	 * but do it only once, not if already getting the history. */
	if ((move_number(id) != b->moves || !board_resized)
	    && !reply_disabled(id) && !is_reset(cmd)) {
		if (UDEBUGL(0))
			fprintf(stderr, "Out of sync, id %d, move %d\n", id, b->moves);
		static char buf[128];
		snprintf(buf, sizeof(buf), "out of sync, move %d expected", b->moves);
		*reply = buf;
		return P_DONE_ERROR;
	}
	return reply_disabled(id) ? P_NOREPLY : P_OK;
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
uct_getstats(struct uct *u, struct board *b, coord_t c, bool keep_looking)
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

/* Set mapping from coordinates to children of the root node. */
static void
find_top_nodes(struct uct *u)
{
	if (!u->t || !u->t->root) return;

	for (struct tree_node *ni = u->t->root->children; ni; ni = ni->sibling) {
		if (!is_pass(ni->coord))
		    u->stats[ni->coord].node = ni;
	}
}

/* genmoves gets in the args parameter
 * "played_games main_time byoyomi_time byoyomi_periods byoyomi_stones"
 * and reads a list of lines "coord playouts value amaf_playouts amaf_value"
 * to get stats of other slaves, except for the first call at a given move number.
 * See uct_getstats() for the description of the return value. */
char *
uct_genmoves(struct engine *e, struct board *b, struct time_info *ti, enum stone color,
	     char *args, bool pass_all_alive)
{
	struct uct *u = e->data;
	assert(u->slave);

	/* Seed the tree if the search is not already running. */
	if (!thread_manager_running) {
		uct_search_setup(u, b, color);
	}

	/* Get playouts and time information from master.
	 * Keep this code in sync with distributed_genmove(). */
	if ((ti->dim == TD_WALLTIME
	     && sscanf(args, "%d %lf %lf %d %d", &u->played_all, &ti->len.t.main_time,
		       &ti->len.t.byoyomi_time, &ti->len.t.byoyomi_periods,
		       &ti->len.t.byoyomi_stones) != 5)

	    || (ti->dim == TD_GAMES && sscanf(args, "%d", &u->played_all) != 1)) {
		return NULL;
	}

	/* Get the move stats if they are present. They are
	 * coord-sorted but the code here doesn't depend on this.
	 * Keep this code in sync with select_best_move(). */

	char line[128];
	while (fgets(line, sizeof(line), stdin) && *line != '\n') {
		char move[64];
		struct move_stats2 s;
		if (sscanf(line, "%63s %d %f %d %f", move,
			   &s.u.playouts, &s.u.value,
			   &s.amaf.playouts, &s.amaf.value) != 5)
			return NULL;
		coord_t *c_ = str2coord(move, board_size(b));
		coord_t c = *c_;
		coord_done(c_);
		assert(!is_pass(c) && !is_resign(c));

		struct node_stats *ns = &u->stats[c];
		if (!ns->node) find_top_nodes(u);
		/* The node may not exist if this slave was behind
		 * but this should be rare so it is not worth creating
		 * the node here. */
		if (!ns->node) {
			if (DEBUGL(2))
				fprintf(stderr, "can't find node %s %d\n", move, c);
			continue;
		}

		/* The master may not send moves below a certain threshold,
		 * but if it sends one it includes the contributions from
		 * all slaves including ours (last_sent_own):
		 *   received_others = received_total - last_sent_own  */
		if (ns->last_sent_own.u.playouts)
			stats_rm_result(&s.u, ns->last_sent_own.u.value,
					ns->last_sent_own.u.playouts);
		if (ns->last_sent_own.amaf.playouts)
			stats_rm_result(&s.amaf, ns->last_sent_own.amaf.value,
					ns->last_sent_own.amaf.playouts);

		/* others_delta = received_others - added_from_others */
		struct move_stats2 delta = s;
		if (ns->added_from_others.u.playouts)
			stats_rm_result(&delta.u, ns->added_from_others.u.value,
					ns->added_from_others.u.playouts);
		/* delta may be <= 0 if some slaves stopped sending this move
		 * because it became below a playouts threshold. In this case
		 * we just keep the old stats in our tree. */
		if (delta.u.playouts <= 0) continue;

		if (ns->added_from_others.amaf.playouts)
			stats_rm_result(&delta.amaf, ns->added_from_others.amaf.value,
					ns->added_from_others.amaf.playouts);

		stats_add_result(&ns->node->u, delta.u.value, delta.u.playouts);
		stats_add_result(&ns->node->amaf, delta.amaf.value, delta.amaf.playouts);
		ns->added_from_others = s;
	}

        /* Continue the Monte Carlo Tree Search. */
	bool keep_looking;
	int base_playouts = u->t->root->u.playouts;
	int played_games = uct_search(u, b, ti, color, u->t, &keep_looking);

	coord_t best_coord;
	uct_search_best(u, b, color, pass_all_alive, played_games, base_playouts, &best_coord);

	char *reply = uct_getstats(u, b, best_coord, keep_looking);
	return reply;
}
