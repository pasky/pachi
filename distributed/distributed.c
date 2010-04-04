/* This is a master for the "distributed" engine. It receives connections
 * from slave machines, sends them gtp commands, then aggregates the
 * results. It can also act as a proxy for the logs of all slave machines.
 * The slave machines must run with engine "uct" (not "distributed").
 * The master sends pachi-genmoves gtp commands regularly to each slave,
 * gets as replies a list of candidate moves, their number of playouts
 * and their value. The master then picks the most popular move. */

/* With time control, the master waits for all slaves, except
 * when the allowed time is already passed. In this case the
 * master picks among the available replies, or waits for just
 * one reply if there is none yet.
 * Without time control, the master waits until the desired
 * number of games have been simulated. In this case the -t
 * parameter for the master should be the sum of the parameters
 * for all slaves. */

/* The master sends updated statistics for the best moves
 * in each genmoves command. In this version only the
 * children of the root node are updated. The slaves
 * reply with just their own stats; they remember what was
 * previously received from or sent to the master, to
 * distinguish their own contribution from that of other slaves. */

/* The master-slave protocol has has fault tolerance. If a slave is
 * out of sync, the master sends it the appropriate command history. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * slave_port=SLAVE_PORT  slaves connect to this port; this parameter is mandatory.
 * max_slaves=MAX_SLAVES  default 100
 * slaves_quit=0|1        quit gtp command also sent to slaves, default false.
 * proxy_port=PROXY_PORT  slaves optionally send their logs to this port.
 *    Warning: with proxy_port, the master stderr mixes the logs of all
 *    machines but you can separate them again:
 *      slave logs:  sed -n '/< .*:/s/.*< /< /p' logfile
 *      master logs: perl -0777 -pe 's/<[ <].*:.*\n//g' logfile
 */

/* A configuration without proxy would have one master run on masterhost as:
 *    zzgo -e distributed slave_port=1234
 * and N slaves running as:
 *    zzgo -e uct -g masterhost:1234 slave
 * With log proxy:
 *    zzgo -e distributed slave_port=1234,proxy_port=1235
 *    zzgo -e uct -g masterhost:1234 -l masterhost:1235 slave
 * If the master itself runs on a machine other than that running gogui,
 * gogui-twogtp, kgsGtp or cgosGtp, it can redirect its gtp port:
 *    zzgo -e distributed -g 10000 slave_port=1234,proxy_port=1235
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <alloca.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define DEBUG

#include "board.h"
#include "engine.h"
#include "move.h"
#include "timeinfo.h"
#include "network.h"
#include "playout.h"
#include "random.h"
#include "stats.h"
#include "mq.h"
#include "debug.h"
#include "distributed/distributed.h"

/* Internal engine state. */
struct distributed {
	char *slave_port;
	char *proxy_port;
	int max_slaves;
	bool slaves_quit;
	struct move my_last_move;
	struct move_stats my_last_stats;
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

/* Max size for one line of reply or slave log. */
#define BSIZE 4096

/* Max size of all gtp commands for one game.
 * 60 chars for the first line of genmoves plus 100 lines
 * of 30 chars each for the stats at last move. */
#define CMDS_SIZE (60*MAX_GAMELEN + 30*100)

/* All gtp commands for current game separated by \n */
static char gtp_cmds[CMDS_SIZE];

/* Latest gtp command sent to slaves. */
static char *gtp_cmd = NULL;

/* Slaves send gtp_cmd when cmd_count changes. */
static int cmd_count = 0;

/* Remember at most 12 gtp ids per move: play pass,
 * 10 genmoves (1s), play pass.
 * For move 0 we always resend the whole history. */
#define MAX_CMDS_PER_MOVE 12

/* History of gtp commands sent for current game, indexed by move. */
static int id_history[MAX_GAMELEN][MAX_CMDS_PER_MOVE];
static char *cmd_history[MAX_GAMELEN][MAX_CMDS_PER_MOVE];

/* Number of active slave machines working for this master. */
static int active_slaves = 0;

/* Number of replies to last gtp command already received. */
static int reply_count = 0;

/* All replies to latest gtp command are in gtp_replies[0..reply_count-1]. */
static char **gtp_replies;

/* Mutex protecting gtp_cmds, gtp_cmd, id_history, cmd_history,
 * cmd_count, active_slaves, reply_count & gtp_replies */
static pthread_mutex_t slave_lock = PTHREAD_MUTEX_INITIALIZER;

/* Condition signaled when a new gtp command is available. */
static pthread_cond_t cmd_cond = PTHREAD_COND_INITIALIZER;

/* Condition signaled when reply_count increases. */
static pthread_cond_t reply_cond = PTHREAD_COND_INITIALIZER;

/* Mutex protecting stderr. Must not be held at same time as slave_lock. */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Absolute time when this program was started.
 * For debugging only. */
static double start_time;

/* Write the time, client address, prefix, and string s to stderr atomically.
 * s should end with a \n */
static void
logline(struct in_addr *client, char *prefix, char *s)
{
	double now = time_now();
	char addr[INET_ADDRSTRLEN];
	if (client) {
		inet_ntop(AF_INET, client, addr, sizeof(addr));
	} else {
		addr[0] = '\0';
	}
	pthread_mutex_lock(&log_lock);
	fprintf(stderr, "%s%15s %9.3f: %s", prefix, addr, now - start_time, s);
	pthread_mutex_unlock(&log_lock);
}

/* Thread opening a connection on the given socket and copying input
 * from there to stderr. */
static void *
proxy_thread(void *arg)
{
	int proxy_sock = (long)arg;
	assert(proxy_sock >= 0);
	for (;;) {
		struct in_addr client;
		int conn = open_server_connection(proxy_sock, &client);
		FILE *f = fdopen(conn, "r");
		char buf[BSIZE];
		while (fgets(buf, BSIZE, f)) {
			logline(&client, "< ", buf);
		}
		fclose(f);
	}
}

/* Get a reply to one gtp command. Return the gtp command id,
 * or -1 if error. reply must have at least CMDS_SIZE bytes.
 * slave_lock is not held on either entry or exit of this function. */
static int
get_reply(FILE *f, struct in_addr client, char *reply)
{
	int reply_id = -1;
	*reply = '\0';
	char *line = reply;
	while (fgets(line, reply + CMDS_SIZE - line, f) && *line != '\n') {
		if (DEBUGL(3) || (DEBUGL(2) && line == reply))
			logline(&client, "<<", line);
		if (reply_id < 0 && (*line == '=' || *line == '?') && isdigit(line[1]))
			reply_id = atoi(line+1);
		line += strlen(line);
	}
	if (*line != '\n') return -1;
	return reply_id;
}

/* Main loop of a slave thread.
 * Send the current command to the slave machine and wait for a reply.
 * Resend command history if the slave machine is out of sync.
 * Returns when the connection with the slave machine is cut.
 * slave_lock is held on both entry and exit of this function. */
static void
slave_loop(FILE *f, struct in_addr client, char *reply_buf, bool resend)
{
	char *to_send = gtp_cmd;
	int last_cmd_sent = 0;
	int last_reply_id = -1;
	int reply_slot = -1;
	for (;;) {
		while (last_cmd_sent == cmd_count && !resend) {
			// Wait for a new gtp command.
			pthread_cond_wait(&cmd_cond, &slave_lock);
			to_send = gtp_cmd;
		}

		/* Command available, send it to slave machine.
		 * If slave was out of sync, send the history. */
		assert(to_send && gtp_cmd);
		char buf[CMDS_SIZE];
		strncpy(buf, to_send, CMDS_SIZE);
		last_cmd_sent = cmd_count;

		pthread_mutex_unlock(&slave_lock);

		if (DEBUGL(1) && resend) {
			if (to_send == gtp_cmds) {
				logline(&client, "? ", "Slave out-of-sync, resending all history\n");
			} else {
				logline(&client, "? ", "Slave behind, partial resend\n");
			}
		}
		fputs(buf, f);
		fflush(f);
		if (DEBUGL(2)) {
			if (!DEBUGL(3)) {
				char *s = strchr(buf, '\n');
				if (s) s[1] = '\0';
			}
			logline(&client, ">>", buf);
		}

		/* Read the reply, which always ends with \n\n
		 * The slave machine sends "=id reply" or "?id reply"
		 * with id == cmd_id if it is in sync. */
		int reply_id = get_reply(f, client, buf);

		pthread_mutex_lock(&slave_lock);
		if (reply_id == -1) return;

		/* Make sure we are still in sync. cmd_count may have
		 * changed but the reply is valid as long as cmd_id didn't
		 * change (this only occurs for consecutive genmoves). */
		int cmd_id = atoi(gtp_cmd);
		if (reply_id == cmd_id && *buf == '=') {
			resend = false;
			strncpy(reply_buf, buf, CMDS_SIZE);
			if (reply_id != last_reply_id)
				reply_slot = reply_count++;
			gtp_replies[reply_slot] = reply_buf;
			last_reply_id = reply_id;

			pthread_cond_signal(&reply_cond);

			/* Force waiting for a new command. The next genmoves
			 * stats we will send must include those just received
			 * (this assumed by the slave). */
			last_cmd_sent = cmd_count;
			continue;
		}
		resend = true;
		to_send = gtp_cmds;
		/* Resend everything if slave got latest command,
		 *  but doesn't have a correct board. */
		if (reply_id == cmd_id) continue;

		/* The slave is ouf-of-sync. Check whether the last command
		 * it received belongs to the current game. If so resend
		 * starting at the last move known by slave, otherwise
		 * resend the whole history. */
		int reply_move = move_number(reply_id);
		if (reply_move > move_number(cmd_id)) continue;

		for (int slot = 0; slot < MAX_CMDS_PER_MOVE; slot++) {
			if (reply_id == id_history[reply_move][slot]) {
				to_send = cmd_history[reply_move][slot];
				break;
			}
		}
	}
}

/* Thread sending gtp commands to one slave machine, and
 * reading replies. If a slave machine dies, this thread waits
 * for a connection from another slave. */
static void *
slave_thread(void *arg)
{
	int slave_sock = (long)arg;
	assert(slave_sock >= 0);
	char reply_buf[CMDS_SIZE];
	bool resend = false;

	for (;;) {
		/* Wait for a connection from any slave. */
		struct in_addr client;
		int conn = open_server_connection(slave_sock, &client);

		FILE *f = fdopen(conn, "r+");
		if (DEBUGL(2))
			logline(&client, "= ", "new slave\n");

		/* Minimal check of the slave identity. */
		fputs("name\n", f);
		if (!fgets(reply_buf, sizeof(reply_buf), f)
		    || strncasecmp(reply_buf, "= Pachi", 7)
		    || !fgets(reply_buf, sizeof(reply_buf), f)
		    || strcmp(reply_buf, "\n")) {
			logline(&client, "? ", "bad slave\n");
			fclose(f);
			continue;
		}

		pthread_mutex_lock(&slave_lock);
		active_slaves++;
		slave_loop(f, client, reply_buf, resend);

		assert(active_slaves > 0);
		active_slaves--;
		// Unblock main thread if it was waiting for this slave.
		pthread_cond_signal(&reply_cond);
		pthread_mutex_unlock(&slave_lock);

		resend = true;
		if (DEBUGL(2))
			logline(&client, "= ", "lost slave\n");
		fclose(f);
	}
}

/* Create a new gtp command for all slaves. The slave lock is held
 * upon entry and upon return, so the command will actually be
 * sent when the lock is released. The last command is overwritten
 * if gtp_cmd points to a non-empty string. cmd is a single word;
 * args has all arguments and is empty or has a trailing \n */
static void
update_cmd(struct board *b, char *cmd, char *args, bool new_id)
{
	assert(gtp_cmd);
	/* To make sure the slaves are in sync, we ignore the original id
	 * and use the board number plus some random bits as gtp id. */
	static int gtp_id = -1;
	int moves = is_reset(cmd) ? 0 : b->moves;
	if (new_id) {
	        /* fast_random() is 16-bit only so the multiplication can't overflow. */
		gtp_id = force_reply(moves + fast_random(65535) * DIST_GAMELEN);
		reply_count = 0;
	}
	snprintf(gtp_cmd, gtp_cmds + CMDS_SIZE - gtp_cmd, "%d %s %s",
		 gtp_id, cmd, *args ? args : "\n");
	cmd_count++;

	/* Remember history for out-of-sync slaves. */
	static int slot = 0;
	slot = (slot + 1) % MAX_CMDS_PER_MOVE;
	id_history[moves][slot] = gtp_id;
	cmd_history[moves][slot] = gtp_cmd;

	// Notify the slave threads about the new command.
	pthread_cond_broadcast(&cmd_cond);
}

/* Update the command history, then create a new gtp command
 * for all slaves. The slave lock is held upon entry and
 * upon return, so the command will actually be sent when the
 * lock is released. cmd is a single word; args has all
 * arguments and is empty or has a trailing \n */
static void
new_cmd(struct board *b, char *cmd, char *args)
{
	// Clear the history when a new game starts:
	if (!gtp_cmd || is_gamestart(cmd)) {
		gtp_cmd = gtp_cmds;
	} else {
		/* Preserve command history for new slaves.
		 * To indicate that the slave should only reply to
		 * the last command we force the id of previous
		 * commands to be just the move number. */
		int id = prevent_reply(atoi(gtp_cmd));
		int len = strspn(gtp_cmd, "0123456789");
		char buf[32];
		snprintf(buf, sizeof(buf), "%0*d", len, id);
		memcpy(gtp_cmd, buf, len);

		gtp_cmd += strlen(gtp_cmd);
	}

	// Let the slave threads send the new gtp command:
	update_cmd(b, cmd, args, true);
}

/* Wait for at least one new reply. Return when all slaves have
 * replied, or when the given absolute time is passed.
 * The replies are returned in gtp_replies[0..reply_count-1]
 * slave_lock is held on entry and on return. */
static void
get_replies(double time_limit)
{
	for (;;) {
		if (reply_count > 0) {
			struct timespec ts;
			double sec;
			ts.tv_nsec = (int)(modf(time_limit, &sec)*1000000000.0);
			ts.tv_sec = (int)sec;
			pthread_cond_timedwait(&reply_cond, &slave_lock, &ts);
		} else {
			pthread_cond_wait(&reply_cond, &slave_lock);
		}
		if (reply_count == 0) continue;
		if (reply_count >= active_slaves) return;
		if (time_now() >= time_limit) break;
	}
	if (DEBUGL(1)) {
		char buf[1024];
		snprintf(buf, sizeof(buf),
			 "get_replies timeout %.3f >= %.3f, replies %d < active %d\n",
			 time_now() - start_time, time_limit - start_time,
			 reply_count, active_slaves);
		logline(NULL, "? ", buf);
	}
	assert(reply_count > 0);
}

/* Maximum time (seconds) to wait for answers to fast gtp commands
 * (all commands except pachi-genmoves and final_status_list). */
#define MAX_FAST_CMD_WAIT 1.0

/* How often to send a stats update to slaves (seconds) */
#define STATS_UPDATE_INTERVAL 0.1 /* 100ms */

/* Maximum time (seconds) to wait between genmoves
 * (all commands except pachi-genmoves and final_status_list). */
#define MAX_FAST_CMD_WAIT 1.0

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
	    || !strcasecmp(cmd, "uct_genbook")
	    || !strcasecmp(cmd, "uct_dumpbook")
	    || !strcasecmp(cmd, "kgs-chat")
	    || !strcasecmp(cmd, "time_left")

	    /* and commands that will be sent to slaves later */
	    || !strcasecmp(cmd, "genmove")
	    || !strcasecmp(cmd, "kgs-genmove_cleanup")
	    || !strcasecmp(cmd, "final_score")
	    || !strcasecmp(cmd, "final_status_list"))
		return P_OK;

	pthread_mutex_lock(&slave_lock);

	// Create a new command to be sent by the slave threads.
	new_cmd(b, cmd, args);

	/* Wait for replies here. If we don't wait, we run the
	 * risk of getting out of sync with most slaves and
	 * sending command history too frequently. */
	get_replies(time_now() + MAX_FAST_CMD_WAIT);

	pthread_mutex_unlock(&slave_lock);
	return P_OK;
}

/* genmoves returns a line "=id played_own total_playouts threads keep_looking[ reserved]"
 * then a list of lines "coord playouts value amaf_playouts amaf_value".
 * Return the move with most playouts, and additional stats.
 * Keep this code in sync with uct/slave.c:report_stats().
 * slave_lock is held on entry and on return. */
static coord_t
select_best_move(struct board *b, struct move_stats2 *stats, int *played,
		 int *total_playouts, int *total_threads, bool *keep_looking)
{
	assert(reply_count > 0);

	/* +2 for pass and resign */
	memset(stats-2, 0, (board_size2(b)+2) * sizeof(*stats));

	coord_t best_move = pass;
	int best_playouts = -1;
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
		// Skip the rest of the firt line if any (allow future extensions)
		r = strchr(r, '\n');

		char move[64];
		struct move_stats2 s;
		while (r && sscanf(++r, "%63s %d %f %d %f", move, &s.u.playouts,
				   &s.u.value, &s.amaf.playouts, &s.amaf.value) == 5) {
			coord_t *c = str2coord(move, board_size(b));
			stats_add_result(&stats[*c].u, s.u.value, s.u.playouts);
			stats_add_result(&stats[*c].amaf, s.amaf.value, s.amaf.playouts);

			if (stats[*c].u.playouts > best_playouts) {
				best_playouts = stats[*c].u.playouts;
				best_move = *c;
			}
			coord_done(c);
			r = strchr(r, '\n');
		}
	}
	*keep_looking = keep > reply_count / 2;
	return best_move;
}

/* Set the args for the genmoves command. If stats is not null,
 * append the stats from all slaves above min_playouts, except
 * for pass and resign. args must have CMDS_SIZE bytes and
 * upon return ends with an empty line.
 * Keep this code in sync with uct_genmoves().
 * slave_lock is held on entry and on return. */
static void
genmoves_args(char *args, struct board *b, enum stone color, int played,
	      struct time_info *ti, struct move_stats2 *stats, int min_playouts)
{
	char *end = args + CMDS_SIZE;
	char *s = args + snprintf(args, CMDS_SIZE, "%s %d", stone2str(color), played);

	if (ti->dim == TD_WALLTIME) {
		s += snprintf(s, end - s, " %.3f %.3f %d %d",
			      ti->len.t.main_time, ti->len.t.byoyomi_time,
			      ti->len.t.byoyomi_periods, ti->len.t.byoyomi_stones);
	}
	s += snprintf(s, end - s, "\n");
	if (stats) {
		foreach_point(b) {
			if (stats[c].u.playouts <= min_playouts) continue;
			s += snprintf(s, end - s, "%s %d %.7f %d %.7f\n",
				      coord2sstr(c, b),
				      stats[c].u.playouts, stats[c].u.value,
				      stats[c].amaf.playouts, stats[c].amaf.value);
		} foreach_point_end;
	}
	s += snprintf(s, end - s, "\n");
}

/* Time control is mostly done by the slaves, so we use default values here. */
#define FUSEKI_END 20
#define YOSE_START 40

static coord_t *
distributed_genmove(struct engine *e, struct board *b, struct time_info *ti,
		    enum stone color, bool pass_all_alive)
{
	struct distributed *dist = e->data;
	double now = time_now();
	double first = now;

	char *cmd = pass_all_alive ? "pachi-genmoves_cleanup" : "pachi-genmoves";
	char args[CMDS_SIZE];

	coord_t best;
	int played, playouts, threads;

	if (ti->period == TT_NULL) *ti = default_ti;
	struct time_stop stop;
	time_stop_conditions(ti, b, FUSEKI_END, YOSE_START, &stop);
	struct time_info saved_ti = *ti;

	/* Send the first genmoves without stats. */
	genmoves_args(args, b, color, 0, ti, NULL, 0);

	/* Combined move stats from all slaves, only for children
	 * of the root node, plus 2 for pass and resign. */
	struct move_stats2 *stats = alloca((board_size2(b)+2) * sizeof(struct move_stats2));
	stats += 2;

	pthread_mutex_lock(&slave_lock);
	new_cmd(b, cmd, args);

	/* Loop until most slaves want to quit or time elapsed. */
	for (;;) {
		double start = now;
		get_replies(now + STATS_UPDATE_INTERVAL);
		now = time_now();
		if (ti->dim == TD_WALLTIME)
			time_sub(ti, now - start);

		bool keep_looking;
		best = select_best_move(b, stats, &played, &playouts, &threads, &keep_looking);

		if (!keep_looking) break;
		if (ti->dim == TD_WALLTIME) {
			if (now - ti->len.t.timer_start >= stop.worst.time) break;
		} else {
			if (played >= stop.worst.playouts) break;
		}
		if (DEBUGL(2)) {
			char buf[BSIZE];
			char *coord = coord2sstr(best, b);
			snprintf(buf, sizeof(buf),
				 "temp winner is %s %s with score %1.4f (%d/%d games)"
				 " %d slaves %d threads\n",
				 stone2str(color), coord, get_value(stats[best].u.value, color),
				 stats[best].u.playouts, playouts, reply_count, threads);
			logline(NULL, "* ", buf);
		}
		/* Send the command with the same gtp id, to avoid discarding
		 * a reply to a previous genmoves at the same move. */
		genmoves_args(args, b, color, played, ti, stats, stats[best].u.playouts / 100);
		update_cmd(b, cmd, args, false);
	}
	int replies = reply_count;

	/* Do not subtract time spent twice (see gtp_parse). */
	*ti = saved_ti;

	dist->my_last_move.color = color;
	dist->my_last_move.coord = best;
	dist->my_last_stats = stats[best].u;

	/* Tell the slaves to commit to the selected move, overwriting
	 * the last "pachi-genmoves" in the command history. */
	char *coord = coord2str(best, b);
	snprintf(args, sizeof(args), "%s %s\n", stone2str(color), coord);
	update_cmd(b, "play", args, true);
	pthread_mutex_unlock(&slave_lock);

	if (DEBUGL(1)) {
		char buf[BSIZE];
		double time = now - first + 0.000001; /* avoid divide by zero */
		snprintf(buf, sizeof(buf),
			 "GLOBAL WINNER is %s %s with score %1.4f (%d/%d games)\n"
			 "genmove %d games in %0.2fs %d slaves %d threads (%d games/s,"
			 " %d games/s/slave, %d games/s/thread)\n",
			 stone2str(color), coord, get_value(stats[best].u.value, color),
			 stats[best].u.playouts, playouts, played, time, replies, threads,
			 (int)(played/time), (int)(played/time/replies),
			 (int)(played/time/threads));
		logline(NULL, "* ", buf);
	}
	free(coord);
	return coord_copy(best);
}

static char *
distributed_chat(struct engine *e, struct board *b, char *cmd)
{
	struct distributed *dist = e->data;
	static char reply[BSIZE];

	cmd += strspn(cmd, " \n\t");
	if (!strncasecmp(cmd, "winrate", 7)) {
		enum stone color = dist->my_last_move.color;
		snprintf(reply, BSIZE, "In %d playouts at %d machines, %s %s can win with %.2f%% probability.",
			 dist->my_last_stats.playouts, active_slaves, stone2str(color),
			 coord2sstr(dist->my_last_move.coord, b),
			 100 * get_value(dist->my_last_stats.value, color));
		return reply;
	}
	return NULL;
}

static int
scmp(const void *p1, const void *p2)
{
	return strcasecmp(*(char * const *)p1, *(char * const *)p2);
}

static void
distributed_dead_group_list(struct engine *e, struct board *b, struct move_queue *mq)
{
	pthread_mutex_lock(&slave_lock);

	new_cmd(b, "final_status_list", "dead\n");
	get_replies(time_now() + MAX_FAST_CMD_WAIT);

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
		coord_t *c = str2coord(dead, board_size(b));
		mq_add(mq, *c);
		coord_done(c);
		dead = strchr(dead, '\n');
	}
	pthread_mutex_unlock(&slave_lock);
}

static struct distributed *
distributed_state_init(char *arg, struct board *b)
{
	struct distributed *dist = calloc2(1, sizeof(struct distributed));

	dist->max_slaves = 100;
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
	int slave_sock = port_listen(dist->slave_port, dist->max_slaves);
	pthread_t thread;
	for (int id = 0; id < dist->max_slaves; id++) {
		pthread_create(&thread, NULL, slave_thread, (void *)(long)slave_sock);
	}

	if (dist->proxy_port) {
		int proxy_sock = port_listen(dist->proxy_port, dist->max_slaves);
		for (int id = 0; id < dist->max_slaves; id++) {
			pthread_create(&thread, NULL, proxy_thread, (void *)(long)proxy_sock);
		}
	}
	return dist;
}

struct engine *
engine_distributed_init(char *arg, struct board *b)
{
	start_time = time_now();
	struct distributed *dist = distributed_state_init(arg, b);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "Distributed Engine";
	e->comment = "I'm playing the distributed engine. When I'm losing, I will resign, "
		"if I think I win, I play until you pass. "
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
