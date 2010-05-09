/* This is the slave specific part of the distributed engine.
 * See introduction at top of distributed/distributed.c.
 * The slave maintains a hash table of nodes received from the
 * master. When receiving stats the hash table gives a pointer to the
 * tree node to update. When sending stats we remember in the tree
 * what was previously sent so that only the incremental part has to
 * be sent.  The incremental part is smaller and can be compressed.
 * The compression is not yet done in this version. */

/* Similarly the master only sends stats increments.
 * They include only contributions from other slaves. */

/* The keys for the hash table are coordinate paths from
 * a root child to a given node. See distributed/distributed.h
 * for the encoding of a path to a 64 bit integer. */

/* To allow the master to select the best move, slaves also send
 * absolute playout counts for the best top level nodes (children
 * of the root node), including contributions from other slaves. */

/* Pass me arguments like a=b,c=d,...
 * Slave specific arguments (see uct.c for the other uct arguments
 * and distributed.c for the port arguments) :
 *  slave                   required to indicate slave mode
 *  max_nodes=MAX_NODES     default 80K
 *  stats_hbits=STATS_HBITS default 24. 2^stats_bits = hash table size
 */

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
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/slave.h"
#include "uct/tree.h"


/* UCT infrastructure for a distributed engine slave. */

/* For debugging only. */
static struct hash_counts h_counts;
static long parent_not_found = 0;
static long parent_leaf = 0;
static long node_not_found = 0;

/* Hash table entry mapping path to node. */
struct tree_hash {
	path_t coord_path;
	struct tree_node *node;
};

void *
uct_htable_alloc(int hbits)
{
	return calloc2(1 << hbits, sizeof(struct tree_hash));
}

/* Clear the hash table. Used only when running as slave for the distributed engine. */
void uct_htable_reset(struct tree *t)
{
	if (!t->htable) return;
	double start = time_now();
	memset(t->htable, 0, (1 << t->hbits) * sizeof(t->htable[0]));
	if (DEBUGL(3))
		fprintf(stderr, "tree occupied %ld %.1f%% inserts %ld collisions %ld/%ld %.1f%% clear %.3fms\n"
			"parent_not_found %.1f%% parent_leaf %.1f%% node_not_found %.1f%%\n",
			h_counts.occupied, h_counts.occupied * 100.0 / (1 << t->hbits),
			h_counts.inserts, h_counts.collisions, h_counts.lookups,
			h_counts.collisions * 100.0 / (h_counts.lookups + 1),
			(time_now() - start)*1000,
			parent_not_found * 100.0 / (h_counts.lookups + 1),
			parent_leaf * 100.0 / (h_counts.lookups + 1),
			node_not_found * 100.0 / (h_counts.lookups + 1));
	if (DEBUG_MODE) h_counts.occupied = 0;
}

/* Find a node given its coord path from root. Insert it in the
 * hash table if it is not already there.
 * Return the tree node, or NULL if the node cannot be found.
 * The tree is modified in background while this function is running.
 * prev is only used to optimize the tree search, given that calls to
 * tree_find_node are made with sorted coordinates (increasing levels
 * and increasing coord within a level). */
static struct tree_node *
tree_find_node(struct tree *t, struct incr_stats *is, struct tree_node *prev)
{
	assert(t && t->htable);
	path_t path = is->coord_path;
	/* pass and resign must never be inserted in the hash table. */
	assert(path > 0);

	int hash, parent_hash;
	bool found;
	find_hash(hash, t->htable, t->hbits, path, found, h_counts);
	struct tree_hash *hnode = &t->htable[hash];

	if (DEBUGVV(7))
		fprintf(stderr,
			"find_node %"PRIpath" %s found %d hash %d playouts %d node %p\n", path,
			path2sstr(path, t->board), found, hash, is->incr.playouts, hnode->node);

	if (found) return hnode->node;

	/* The master sends parents before children so the parent should
	 * already be in the hash table. */
	path_t parent_p = parent_path(path, t->board);
	struct tree_node *parent;
	if (parent_p) {
		find_hash(parent_hash, t->htable, t->hbits,
			  parent_p, found, h_counts);
		parent = t->htable[parent_hash].node;
	} else {
		parent = t->root;
	}
	struct tree_node *node = NULL;
	if (parent) {
		/* Search for the node in parent's children. */
		coord_t leaf = leaf_coord(path, t->board);
		node = (prev && prev->parent == parent ? prev->sibling : parent->children);
		while (node && node->coord != leaf) node = node->sibling;

		if (DEBUG_MODE) parent_leaf += !parent->is_expanded;
	} else {
		if (DEBUG_MODE) parent_not_found++;
		if (DEBUGVV(7))
			fprintf(stderr, "parent of %"PRIpath" %s not found\n",
				path, path2sstr(path, t->board));
	}

	/* Insert the node in the hash table. */
	hnode->node = node;
	if (DEBUG_MODE) h_counts.inserts++, h_counts.occupied++;
	if (DEBUGVV(7))
		fprintf(stderr, "insert path %"PRIpath" %s hash %d playouts %d node %p\n",
			path, path2sstr(path, t->board), hash, is->incr.playouts, node);

	if (DEBUG_MODE && !node) node_not_found++;

	hnode->coord_path = path;
	return node;
}


/* Read and discard any binary arguments. The number of
 * bytes to be skipped is given by @size in the command. */
static void
discard_bin_args(char *args)
{
	char *s = strchr(args, '@');
	int size = 0;
	if (s) size = atoi(s+1);
	while (size) {
		char buf[64*1024];
		int len = sizeof(buf);
		if (len > size) len = size;
		len = fread(buf, 1, len, stdin);
		if (len <= 0) break;
		size -= len;
	}
}

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
		discard_bin_args(args);

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
	if (nodes > (1 << u->stats_hbits)) return false;

	struct tree *t = u->t;
	assert(nodes && t->htable);
	struct tree_node *prev = NULL;
	double start_time = time_now();

	for (int n = 0; n < nodes; n++) {
		struct incr_stats is;
		if (fread(&is, sizeof(struct incr_stats), 1, stdin) != 1)
			return false;

		if (UDEBUGL(7))
			fprintf(stderr, "read %5d/%d %6d %.3f %"PRIpath" %s\n", n, nodes,
				is.incr.playouts, is.incr.value, is.coord_path,
				path2sstr(is.coord_path, t->board));

		struct tree_node *node = tree_find_node(t, &is, prev);
		if (!node) continue;

		/* node_total += others_incr */
		stats_add_result(&node->u, is.incr.value, is.incr.playouts);

		/* last_total += others_incr */
		stats_add_result(&node->pu, is.incr.value, is.incr.playouts);

		prev = node;
	}
	if (DEBUGVV(2))
		fprintf(stderr, "read args for %d nodes in %.4fms\n", nodes,
			(time_now() - start_time)*1000);
	return true;
}

/* Get stats for the distributed engine. Return a buffer with one
 * line "played_own root_playouts threads keep_looking", then
 * a list of lines "coord playouts value" with absolute counts for
 * children of the root node (including contributions from other
 * slaves). The last line must not end with \n.
 * If c is pass or resign, add this move with root->playouts weight.
 * This function is called only by the main thread, but may be
 * called while the tree is updated by the worker threads. Keep this
 * code in sync with distributed/distributed.c:select_best_move(). */
static char *
report_stats(struct uct *u, struct board *b, coord_t c, bool keep_looking)
{
	static char reply[10240];
	char *r = reply;
	char *end = reply + sizeof(reply);
	struct tree_node *root = u->t->root;
	r += snprintf(r, end - r, "%d %d %d %d", u->played_own, root->u.playouts, u->threads, keep_looking);
	int min_playouts = root->u.playouts / 100;

	/* Give a large weight to pass or resign, but still allow other moves. */
	if (is_pass(c) || is_resign(c))
		r += snprintf(r, end - r, "\n%s %d %.1f", coord2sstr(c, b),
			      root->u.playouts, 0.0);

	/* We rely on the fact that root->children is set only
	 * after all children are created. */
	for (struct tree_node *ni = root->children; ni; ni = ni->sibling) {

		if (is_pass(ni->coord)) continue;
		if (ni->u.playouts <= min_playouts || ni->hints & TREE_HINT_INVALID)
			continue;

		assert(ni->coord > 0 && ni->coord < board_size2(b));
		char buf[4];
		/* We return the values as stored in the tree, so from black's view. */
		r += snprintf(r, end - r, "\n%s %d %.7f", coord2bstr(buf, ni->coord, b),
			      ni->u.playouts, ni->u.value);
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
