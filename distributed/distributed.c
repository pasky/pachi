/* This is a master for the "distributed" engine. It receives connections
 * from slave machines, sends them gtp commands, then aggregates the
 * results. It can also act as a proxy for the logs of all slave machines.
 * The slave machines must run with engine "uct" (not "distributed").
 * The master sends pachi-genmoves gtp commands regularly to each slave,
 * gets as replies a list of nodes, their number of playouts
 * and their value. The master then picks the most popular move
 * among the top level nodes. */

/* With time control, the master waits for all slaves, except
 * when the allowed time is already passed. In this case the
 * master picks among the available replies, or waits for just
 * one reply if there is none yet.
 * Without time control, the master waits until the desired
 * number of games have been simulated. In this case the -t
 * parameter for the master should be the sum of the parameters
 * for all slaves. */

/* The master sends updated statistics for the best nodes in each
 * genmoves command. They are incremental updates from all other
 * slaves (so they exclude contributions from the target slave).
 * The slaves reply with just their own stats. So both master and
 * slave remember what was previously sent. A slave remembers in
 * the tree ("pu" field), which is stable across moves. The slave
 * also has a temporary hash table to map received coord paths
 * to tree nodes; the hash table is cleared at each new move.
 * The master remembers stats in a queue of received buffers that
 * are merged together, plus one hash table per slave. The master
 * queue and the hash tables are cleared at each new move. */

/* To allow the master to select the best move, slaves also send
 * absolute playout counts for the best top level nodes (children
 * of the root node), including contributions from other slaves.
 * The master sums these counts and picks the best sum, which is
 * equivalent to picking the best average. (The master cannot
 * use the incremental stats sent in binary form because they
 * are not maintained across moves, so playouts from previous
 * moves would be lost.) */

/* The master-slave protocol has fault tolerance. If a slave is
 * out of sync, the master sends it the appropriate command history. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * slave_port=SLAVE_PORT     slaves connect to this port; this parameter is mandatory.
 * max_slaves=MAX_SLAVES     default 24
 * shared_nodes=SHARED_NODES default 10K
 * stats_hbits=STATS_HBITS   default 21. 2^stats_bits = hash table size
 * slaves_quit=0|1           quit gtp command also sent to slaves, default false.
 * proxy_port=PROXY_PORT     slaves optionally send their logs to this port.
 *    Warning: with proxy_port, the master stderr mixes the logs of all
 *    machines but you can separate them again:
 *      slave logs:  sed -n '/< .*:/s/.*< /< /p' logfile
 *      master logs: perl -0777 -pe 's/<[ <].*:.*\n//g' logfile
 */

/* A configuration without proxy would have one master run on masterhost as:
 *    pachi -e distributed slave_port=1234
 * and N slaves running as:
 *    pachi -e uct -g masterhost:1234 slave
 * With log proxy:
 *    pachi -e distributed slave_port=1234,proxy_port=1235
 *    pachi -e uct -g masterhost:1234 -l masterhost:1235 slave
 * If the master itself runs on a machine other than that running gogui,
 * gogui-twogtp, kgsGtp or cgosGtp, it can redirect its gtp port:
 *    pachi -e distributed -g 10000 slave_port=1234,proxy_port=1235
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#define DEBUG

#include "engine.h"
#include "move.h"
#include "timeinfo.h"
#include "playout.h"
#include "stats.h"
#include "mq.h"
#include "debug.h"
#include "chat.h"
#include "distributed/distributed.h"
#include "distributed/merge.h"

/* Internal engine state. */
struct distributed {
	char *slave_port;
	char *proxy_port;
	int max_slaves;
	int shared_nodes;
	int stats_hbits;
	bool slaves_quit;
	struct move my_last_move;
	struct move_stats my_last_stats;
	int slaves;
	int threads;
};

/* Default number of simulations to perform per move.
 * Note that this is in total over all slaves! */
#define DIST_GAMES	80000
static const struct time_info default_ti = {
	.period = TT_MOVE,
	.dim = TD_GAMES,
	.len = { .games = DIST_GAMES },
};

#define get_value(value, color) \
	((color) == S_BLACK ? (value) : 1 - (value))


/* Maximum time (seconds) to wait for answers to fast gtp commands
 * (all commands except pachi-genmoves and final_status_list). */
#define MAX_FAST_CMD_WAIT 0.5

/* Maximum time (seconds) to wait for answers to genmoves. */
#define MAX_GENMOVES_WAIT 0.1 /* 100 ms */

/* Minimum time (seconds) to wait before we stop early. This should
 * ensure that most slaves have replied at least once. */
#define MIN_EARLY_STOP_WAIT 0.3 /* 300 ms */

/* Display a path as leaf<parent<grandparent...
 * Returns the path string in a static buffer; it is NOT safe for
 * anything but debugging - in particular, it is NOT thread-safe! */
char *
path2sstr(path_t path, struct board *b)
{
	/* Special case for pass and resign. */
	if (path < 0) return coord2sstr((coord_t)path, b);

	static char buf[16][64];
	static int bi = 0;
	char *b2;
	b2 = buf[bi++ & 15];
	*b2 = '\0';
	char *s = b2;
	char *end = b2 + 64;
	coord_t leaf;
	while ((leaf = leaf_coord(path, b)) != 0) {
		s += snprintf(s, end - s, "%s<", coord2sstr(leaf, b));
		path = parent_path(path, b);
	}
	if (s != b2) s[-1] = '\0';
	return b2;
}

/* Dispatch a new gtp command to all slaves.
 * The slave lock must not be held upon entry and is released upon return.
 * args is empty or ends with '\n' */
static enum parse_code
distributed_notify(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply)
{
	struct distributed *dist = e->data;

	/* Commands that should not be sent to slaves.
	 * time_left will be part of next pachi-genmoves,
	 * we reduce latency by not forwarding it here. */
	if ((!strcasecmp(cmd, "quit") && !dist->slaves_quit)
	    || !strcasecmp(cmd, "pachi-gentbook")
	    || !strcasecmp(cmd, "pachi-dumptbook")
	    || !strcasecmp(cmd, "kgs-chat")
	    || !strcasecmp(cmd, "time_left")

	    /* and commands that will be sent to slaves later */
	    || !strcasecmp(cmd, "genmove")
	    || !strcasecmp(cmd, "kgs-genmove_cleanup")
	    || !strcasecmp(cmd, "final_score")
	    || !strcasecmp(cmd, "final_status_list"))
		return P_OK;

	protocol_lock();

	// Create a new command to be sent by the slave threads.
	new_cmd(b, cmd, args);

	/* Wait for replies here. If we don't wait, we run the
	 * risk of getting out of sync with most slaves and
	 * sending command history too frequently. But don't wait
	 * for all slaves otherwise we can lose on time because of
	 * a single slow slave when replaying a whole game. */
	int min_slaves = active_slaves > 1 ? 3 * active_slaves / 4 : 1;
	get_replies(time_now() + MAX_FAST_CMD_WAIT, min_slaves);

	protocol_unlock();

	// At the beginning wait even more for late slaves.
	if (b->moves == 0) sleep(1);
	return P_OK;
}

/* The playouts sent by slaves for the children of the root node
 * include contributions from other slaves. To avoid 32-bit overflow on
 * large configurations with many slaves we must average the playouts. */
struct large_stats {
	long playouts; // # of playouts
	floating_t value; // BLACK wins/playouts
};

static void
large_stats_add_result(struct large_stats *s, floating_t result, long playouts)
{
	s->playouts += playouts;
	s->value += (result - s->value) * playouts / s->playouts;
}

/* genmoves returns "=id played_own total_playouts threads keep_looking @size"
 * then a list of lines "coord playouts value" with absolute counts for
 * children of the root node, then a binary array of incr_stats structs.
 * To simplify the code, we assume that master and slave have the same architecture
 * (store values identically).
 * Return the move with most playouts, and additional stats.
 * keep_looking is set from a majority vote of the slaves seen so far for this
 * move but should not be trusted if too few slaves have been seen.
 * Keep this code in sync with uct/slave.c:report_stats().
 * slave_lock is held on entry and on return. */
static coord_t
select_best_move(struct board *b, struct large_stats *stats, int *played,
		 int *total_playouts, int *total_threads, bool *keep_looking)
{
	assert(reply_count > 0);

	/* +2 for pass and resign */
	memset(stats-2, 0, (board_size2(b)+2) * sizeof(*stats));

	coord_t best_move = pass;
	long best_playouts = 0;
	*played = 0;
	*total_playouts = 0;
	*total_threads = 0;
	int keep = 0;

	for (int reply = 0; reply < reply_count; reply++) {
		char *r = gtp_replies[reply];
		int id, o, p, t, k;
		if (sscanf(r, "=%d %d %d %d %d", &id, &o, &p, &t, &k) != 5) continue;
		*played += o;
		*total_playouts += p;
		*total_threads += t;
		keep += k;
		// Skip the rest of the firt line in particular @size
		r = strchr(r, '\n');

		char move[64];
		struct move_stats s;
		while (r && sscanf(++r, "%63s %d " PRIfloating, move, &s.playouts, &s.value) == 3) {
			coord_t c = str2scoord(move, board_size(b));
			assert (c >= resign && c < board_size2(b) && s.playouts >= 0);

			large_stats_add_result(&stats[c], s.value, (long)s.playouts);

			if (stats[c].playouts > best_playouts) {
				best_playouts = stats[c].playouts;
				best_move = c;
			}
			r = strchr(r, '\n');
		}
	}
	for (coord_t c = resign; c < board_size2(b); c++)
		stats[c].playouts /= reply_count;
	*keep_looking = keep > reply_count / 2;
	return best_move;
}

/* Set the args for the genmoves command. If binary_args is set,
 * each slave thred will add the correct binary size when sending
 * (see get_binary_arg()). args must have CMDS_SIZE bytes and
 * upon return ends with a single \n.
 * Keep this code in sync with uct/slave.c:uct_genmoves().
 * slave_lock is held on entry and on return but we don't
 * rely on the lock here. */
static void
genmoves_args(char *args, enum stone color, int played,
	      struct time_info *ti, bool binary_args)
{
	char *end = args + CMDS_SIZE;
	char *s = args + snprintf(args, CMDS_SIZE, "%s %d", stone2str(color), played);

	if (ti->dim == TD_WALLTIME) {
		s += snprintf(s, end - s, " %.3f %.3f %d %d",
			      ti->len.t.main_time, ti->len.t.byoyomi_time,
			      ti->len.t.byoyomi_periods, ti->len.t.byoyomi_stones);
	}
	s += snprintf(s, end - s, binary_args ? " @0\n" : "\n");
}

/* Time control is mostly done by the slaves, so we use default values here. */
#define FUSEKI_END 20
#define YOSE_START 40
#define MAX_MAINTIME_RATIO 3.0

/* Regularly send genmoves command to the slaves, and select the best move. */
static coord_t *
distributed_genmove(struct engine *e, struct board *b, struct time_info *ti,
		    enum stone color, bool pass_all_alive)
{
	struct distributed *dist = e->data;
	double now = time_now();
	double first = now;
	char buf[BSIZE]; // debug only

	char *cmd = pass_all_alive ? "pachi-genmoves_cleanup" : "pachi-genmoves";
	char args[CMDS_SIZE];

	coord_t best;
	int played, playouts, threads;

	if (ti->period == TT_NULL) *ti = default_ti;
	struct time_stop stop;
	time_stop_conditions(ti, b, FUSEKI_END, YOSE_START, MAX_MAINTIME_RATIO, &stop);
	struct time_info saved_ti = *ti;

	/* Combined move stats from all slaves, only for children
	 * of the root node, plus 2 for pass and resign. */
	struct large_stats stats_array[board_size2(b) + 2], *stats;
	stats = &stats_array[2];

	protocol_lock();
	clear_receive_queue();

	/* Send the first genmoves without stats. */
	genmoves_args(args, color, 0, ti, false);
	new_cmd(b, cmd, args);

	/* Loop until most slaves want to quit or time elapsed. */
	int iterations;
	for (iterations = 1; ; iterations++) {
		double start = now;
		/* Wait for just one slave to get stats as fresh as possible,
		 * or at most 100ms to check if we run out of time. */
		get_replies(now + MAX_GENMOVES_WAIT, 1);
		now = time_now();
		if (ti->dim == TD_WALLTIME)
			time_sub(ti, now - start, false);

		bool keep_looking;
		best = select_best_move(b, stats, &played, &playouts, &threads, &keep_looking);

		if (ti->dim == TD_WALLTIME) {
			if (now - ti->len.t.timer_start >= stop.worst.time) break;
			if (!keep_looking && now - first >= MIN_EARLY_STOP_WAIT) break;
		} else {
			if (!keep_looking || played >= stop.worst.playouts) break;
		}
		if (DEBUGVV(2)) {
			char *coord = coord2sstr(best, b);
			snprintf(buf, sizeof(buf),
				 "temp winner is %s %s with score %1.4f (%d/%d games)"
				 " %d slaves %d threads\n",
				 stone2str(color), coord, get_value(stats[best].value, color),
				 (int)stats[best].playouts, playouts, reply_count, threads);
			logline(NULL, "* ", buf);
		}
		/* Send the command with the same gtp id, to avoid discarding
		 * a reply to a previous genmoves at the same move. */
		genmoves_args(args, color, played, ti, true);
		update_cmd(b, cmd, args, false);
	}
	int replies = reply_count;

	/* Do not subtract time spent twice (see gtp_parse). */
	*ti = saved_ti;

	dist->my_last_move.color = color;
	dist->my_last_move.coord = best;
	dist->my_last_stats.value = stats[best].value;
	dist->my_last_stats.playouts = (int)stats[best].playouts;
	dist->slaves = reply_count;
	dist->threads = threads;

	/* Tell the slaves to commit to the selected move, overwriting
	 * the last "pachi-genmoves" in the command history. */
	clear_receive_queue();
	char coordbuf[4];
	char *coord = coord2bstr(coordbuf, best, b);
	snprintf(args, sizeof(args), "%s %s\n", stone2str(color), coord);
	update_cmd(b, "play", args, true);
	protocol_unlock();

	if (DEBUGL(1)) {
		double time = now - first + 0.000001; /* avoid divide by zero */
		snprintf(buf, sizeof(buf),
			 "GLOBAL WINNER is %s %s with score %1.4f (%d/%d games)\n"
			 "genmove %d games in %0.2fs %d slaves %d threads (%d games/s,"
			 " %d games/s/slave, %d games/s/thread, %.3f ms/iter)\n",
			 stone2str(color), coord, get_value(stats[best].value, color),
			 (int)stats[best].playouts, playouts, played, time, replies, threads,
			 (int)(played/time), (int)(played/time/replies),
			 (int)(played/time/threads), 1000*time/iterations);
		logline(NULL, "* ", buf);
	}
	if (DEBUGL(3)) {
		int total_hnodes = replies * (1 << dist->stats_hbits);
		merge_print_stats(total_hnodes);
	}
	return coord_copy(best);
}

static char *
distributed_chat(struct engine *e, struct board *b, bool opponent, char *from, char *cmd)
{
	struct distributed *dist = e->data;
	double winrate = get_value(dist->my_last_stats.value, dist->my_last_move.color);

	return generic_chat(b, opponent, from, cmd, dist->my_last_move.color, dist->my_last_move.coord,
			    dist->my_last_stats.playouts, dist->slaves, dist->threads, winrate, 0.0);
}

static int
scmp(const void *p1, const void *p2)
{
	return strcasecmp(*(char * const *)p1, *(char * const *)p2);
}

static void
distributed_dead_group_list(struct engine *e, struct board *b, struct move_queue *mq)
{
	protocol_lock();

	new_cmd(b, "final_status_list", "dead\n");
	get_replies(time_now() + MAX_FAST_CMD_WAIT, active_slaves);

	/* Find the most popular reply. */
	qsort(gtp_replies, reply_count, sizeof(char *), scmp);
	int best_reply = 0;
	int best_count = 1;
	int count = 1;
	for (int reply = 1; reply < reply_count; reply++) {
		if (!strcmp(gtp_replies[reply], gtp_replies[reply-1])) {
			count++;
		} else {
			count = 1;
		}
		if (count > best_count) {
			best_count = count;
			best_reply = reply;
		}
	}

	/* Pick the first move of each line as group. */
	char *dead = gtp_replies[best_reply];
	dead = strchr(dead, ' '); // skip "id "
	while (dead && *++dead != '\n') {
		mq_add(mq, str2scoord(dead, board_size(b)), 0);
		dead = strchr(dead, '\n');
	}
	protocol_unlock();
}

static struct distributed *
distributed_state_init(char *arg, struct board *b)
{
	struct distributed *dist = calloc2(1, sizeof(struct distributed));

	dist->stats_hbits = DEFAULT_STATS_HBITS;
	dist->max_slaves = DEFAULT_MAX_SLAVES;
	dist->shared_nodes = DEFAULT_SHARED_NODES;
	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "slave_port") && optval) {
				dist->slave_port = strdup(optval);
			} else if (!strcasecmp(optname, "proxy_port") && optval) {
				dist->proxy_port = strdup(optval);
			} else if (!strcasecmp(optname, "max_slaves") && optval) {
				dist->max_slaves = atoi(optval);
			} else if (!strcasecmp(optname, "shared_nodes") && optval) {
				/* Share at most shared_nodes between master and slave at each genmoves.
				 * Must use the same value in master and slaves. */
				dist->shared_nodes = atoi(optval);
			} else if (!strcasecmp(optname, "stats_hbits") && optval) {
                                /* Set hash table size to 2^stats_hbits for the shared stats. */
				dist->stats_hbits = atoi(optval);
			} else if (!strcasecmp(optname, "slaves_quit")) {
				dist->slaves_quit = !optval || atoi(optval);
			} else {
				fprintf(stderr, "distributed: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	gtp_replies = calloc2(dist->max_slaves, sizeof(char *));

	if (!dist->slave_port) {
		fprintf(stderr, "distributed: missing slave_port\n");
		exit(1);
	}

	merge_init(&default_sstate, dist->shared_nodes, dist->stats_hbits, dist->max_slaves);
	protocol_init(dist->slave_port, dist->proxy_port, dist->max_slaves);

	return dist;
}

struct engine *
engine_distributed_init(char *arg, struct board *b)
{
	struct distributed *dist = distributed_state_init(arg, b);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "Distributed";
	e->comment = "If you believe you have won but I am still playing, "
		"please help me understand by capturing all dead stones. "
		"Anyone can send me 'winrate' in private chat to get my assessment of the position.";
	e->notify = distributed_notify;
	e->genmove = distributed_genmove;
	e->dead_group_list = distributed_dead_group_list;
	e->chat = distributed_chat;
	e->data = dist;
	// Keep the threads and the open socket connections:
	e->keep_on_clear = true;

	return e;
}
