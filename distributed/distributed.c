/* This is a master for the "distributed" engine. It receives connections
 * from slave machines, sends them gtp commands, then aggregates the
 * results. It can also act as a proxy for the logs of all slave machines.
 * The slave machines must run with engine "uct" (not "distributed").
 * The master sends the pachi-genmoves gtp command to each slave,
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

/* This first version does not send tree updates between slaves,
 * but it has fault tolerance. If a slave is out of sync, the master
 * sends it the appropriate command history. */

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

static coord_t select_best_move(struct board *b, struct move_stats *best_stats,
				int *total_playouts, int *total_threads);

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

/* Max size for one reply or slave log. */
#define BSIZE 4096

/* Max size of all gtp commands for one game */
#define CMDS_SIZE (40*MAX_GAMELEN)

/* All gtp commands for current game separated by \n */
char gtp_cmds[CMDS_SIZE];

/* Latest gtp command sent to slaves. */
char *gtp_cmd = NULL;

/* Remember at most 3 gtp ids per move (time_left, genmoves, play).
 * For move 0 there can be more than 3 commands
 * but then we resend the whole history. */
#define MAX_CMDS_PER_MOVE 3

/* History of gtp commands sent for current game, indexed by move. */
int id_history[MAX_GAMELEN][MAX_CMDS_PER_MOVE];
char *cmd_history[MAX_GAMELEN][MAX_CMDS_PER_MOVE];

/* Number of active slave machines working for this master. */
int active_slaves = 0;

/* Number of replies to last gtp command already received. */
int reply_count = 0;

/* All replies to latest gtp command are in gtp_replies[0..reply_count-1]. */
char **gtp_replies;

/* Mutex protecting gtp_cmds, gtp_cmd, id_history, cmd_history,
 * active_slaves, reply_count & gtp_replies */
pthread_mutex_t slave_lock = PTHREAD_MUTEX_INITIALIZER;

/* Condition signaled when a new gtp command is available. */
static pthread_cond_t cmd_cond = PTHREAD_COND_INITIALIZER;

/* Condition signaled when reply_count increases. */
static pthread_cond_t reply_cond = PTHREAD_COND_INITIALIZER;

/* Mutex protecting stderr. Must not be held at same time as slave_lock. */
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Absolute time when this program was started.
 * For debugging only. */
double start_time;

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

/* Main loop of a slave thread.
 * Send the current command to the slave machine and wait for a reply.
 * Resend command history if the slave machine is out of sync.
 * Returns when the connection with the slave machine is cut.
 * slave_lock is held on both entry and exit of this function. */
static void
slave_loop(FILE *f, struct in_addr client, char *buf, bool resend)
{
	char *to_send = gtp_cmd;
	int cmd_id = -1;
	int reply_id = -1;
	for (;;) {
		while (cmd_id == reply_id && !resend) {
			// Wait for a new gtp command.
			pthread_cond_wait(&cmd_cond, &slave_lock);
			if (gtp_cmd)
				cmd_id = atoi(gtp_cmd);
			to_send = gtp_cmd;
		}

		/* Command available, send it to slave machine.
		 * If slave was out of sync, send the history. */
		assert(to_send && gtp_cmd);
		strncpy(buf, to_send, CMDS_SIZE);
		cmd_id = atoi(gtp_cmd);

		pthread_mutex_unlock(&slave_lock);

		if (DEBUGL(1) && resend) {
			if (to_send == gtp_cmds) {
				logline(&client, "? ", "Slave out-of-sync, resending all history\n");
			} else {
				logline(&client, "? ", "Slave behind, partial resend\n");
			}
		}
		if (DEBUGL(2))
			logline(&client, ">>", buf);
		fputs(buf, f);
		fflush(f);

		/* Read the reply, which always ends with \n\n
		 * The slave machine sends "=id reply" or "?id reply"
		 * with id == cmd_id if it is in sync. */
		*buf = '\0';
		reply_id = -1;
		char *line = buf;
		while (fgets(line, buf + CMDS_SIZE - line, f) && *line != '\n') {
			if (DEBUGL(2))
				logline(&client, "<<", line);
			if (reply_id < 0 && (*line == '=' || *line == '?') && isdigit(line[1]))
				reply_id = atoi(line+1);
			line += strlen(line);
		}

		pthread_mutex_lock(&slave_lock);
		if (*line != '\n') return;
		// Make sure we are still in sync:
		cmd_id = atoi(gtp_cmd);
		if (reply_id == cmd_id && *buf == '=') {
			resend = false;
			gtp_replies[reply_count++] = buf;
			pthread_cond_signal(&reply_cond);
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
	char slave_buf[CMDS_SIZE];
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
		if (!fgets(slave_buf, sizeof(slave_buf), f)
		    || strncasecmp(slave_buf, "= Pachi", 7)
		    || !fgets(slave_buf, sizeof(slave_buf), f)
		    || strcmp(slave_buf, "\n")) {
			logline(&client, "? ", "bad slave\n");
			fclose(f);
			continue;
		}

		pthread_mutex_lock(&slave_lock);
		active_slaves++;
		slave_loop(f, client, slave_buf, resend);

		assert(active_slaves > 0);
		active_slaves--;
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
update_cmd(struct board *b, char *cmd, char *args)
{
	assert(gtp_cmd);
	/* To make sure the slaves are in sync, we ignore the original id
	 * and use the board number plus some random bits as gtp id.
	 * Make sure the new command has a new id otherwise slaves
	 * won't send it. */
	static int gtp_id = -1;
	int id;
	int moves = is_reset(cmd) ? 0 : b->moves;
	do {
	        /* fast_random() is 16-bit only so the multiplication can't overflow. */
		id = force_reply(moves + fast_random(65535) * DIST_GAMELEN);
	} while (id == gtp_id);
	gtp_id = id;
	snprintf(gtp_cmd, gtp_cmds + CMDS_SIZE - gtp_cmd, "%d %s %s",
		 id, cmd, *args ? args : "\n");
	reply_count = 0;

	/* Remember history for out-of-sync slaves, at most 3 ids per move
         * (time_left, genmoves, play). */
	static int slot = 0;
	slot = (slot + 1) % MAX_CMDS_PER_MOVE;
	id_history[moves][slot] = id;
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
	update_cmd(b, cmd, args);
}

/* If time_limit > 0, wait until all slaves have replied, or if the
 * given absolute time is passed, wait for at least one reply.
 * If time_limit == 0, wait until we get at least min_playouts games
 * simulated in total by all the slaves, or until all slaves have replied.
 * The replies are returned in gtp_replies[0..reply_count-1]
 * slave_lock is held on entry and on return. */
static void
get_replies(double time_limit, int min_playouts, struct board *b)
{
	while (reply_count == 0 || reply_count < active_slaves) {
		if (time_limit && reply_count > 0) {
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
		if (time_limit) {
			if (time_now() >= time_limit) break;
		} else {
			int playouts, threads;
			struct move_stats s;
			select_best_move(b, &s, &playouts, &threads);
			if (playouts >= min_playouts) return;
		}
	}
	if (DEBUGL(1)) {
		char buf[1024];
		snprintf(buf, sizeof(buf),
			 "get_replies timeout %.3f >= %.3f, replies %d < active %d\n",
			 time_now() - start_time, time_limit - start_time, reply_count, active_slaves);
		logline(NULL, "? ", buf);
	}
	assert(reply_count > 0);
}

/* Maximum time (seconds) to wait for answers to fast gtp commands
 * (all commands except pachi-genmoves and final_status_list). */
#define MAX_FAST_CMD_WAIT 1.0

/* Dispatch a new gtp command to all slaves.
 * The slave lock must not be held upon entry and is released upon return.
 * args is empty or ends with '\n' */
static enum parse_code
distributed_notify(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply)
{
	struct distributed *dist = e->data;

	/* Commands that should not be sent to slaves */
	if ((!strcasecmp(cmd, "quit") && !dist->slaves_quit)
	    || !strcasecmp(cmd, "uct_genbook")
	    || !strcasecmp(cmd, "uct_dumpbook")
	    || !strcasecmp(cmd, "kgs-chat")

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
	get_replies(time_now() + MAX_FAST_CMD_WAIT, 0, b);

	pthread_mutex_unlock(&slave_lock);
	return P_OK;
}

/* pachi-genmoves returns a line "=id total_playouts threads[ reserved]" then a list of lines
 * "coord playouts value". Keep this function in sync with uct_notify().
 * Return the move with most playouts, its average value, and stats for debugging.
 * slave_lock is held on entry and on return. */
static coord_t
select_best_move(struct board *b, struct move_stats *best_stats,
		 int *total_playouts, int *total_threads)
{
	assert(reply_count > 0);

	/* +2 for pass and resign. */
	struct move_stats *stats = alloca((board_size2(b)+2) * sizeof(struct move_stats));
	memset(stats, 0, (board_size2(b)+2) * sizeof(*stats));
	stats += 2;

	coord_t best_move = pass;
	int best_playouts = -1;
	*total_playouts = *total_threads = 0;

	for (int reply = 0; reply < reply_count; reply++) {
		char *r = gtp_replies[reply];
		int id, playouts, threads;
		if (sscanf(r, "=%d %d %d", &id, &playouts, &threads) != 3) continue;
		*total_playouts += playouts;
		*total_threads += threads;
		// Skip the rest of the firt line if any (allow future extensions)
		r = strchr(r, '\n');

		char move[64];
		struct move_stats s;
		while (r && sscanf(++r, "%63s %d %f", move, &s.playouts, &s.value) == 3) {
			coord_t *c = str2coord(move, board_size(b));
			stats_add_result(&stats[*c], s.value, s.playouts);
			if (stats[*c].playouts > best_playouts) {
				best_playouts = stats[*c].playouts;
				best_move = *c;
			}			  
			coord_done(c);
			r = strchr(r, '\n');
		}
	}
	*best_stats = stats[best_move];
	return best_move;
}

/* Time control is mostly done by the slaves, so we use default values here. */
#define FUSEKI_END 20
#define YOSE_START 40

static coord_t *
distributed_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	struct distributed *dist = e->data;
	double start = time_now();

	long time_limit = 0;
	int min_playouts = 0;

	char *cmd = pass_all_alive ? "pachi-genmoves_cleanup" : "pachi-genmoves";
	char args[128];

	if (ti->period == TT_NULL) *ti = default_ti;
	struct time_stop stop;
	time_stop_conditions(ti, b, FUSEKI_END, YOSE_START, &stop);

	if (ti->dim == TD_WALLTIME) {
		time_limit = ti->len.t.timer_start + stop.worst.time;

		/* Send time info to the slaves to make sure they all
		 * reply in time, particularly if they were out of sync
		 * and there are no time_left commands. We cannot send
		 * the absolute time limit because slaves may have a
		 * different system time.
		 * Keep this code in sync with gtp_parse(). */
		snprintf(args, sizeof(args), "%s %.3f %.3f %d %d\n",
			 stone2str(color), ti->len.t.main_time,
			 ti->len.t.byoyomi_time, ti->len.t.byoyomi_periods,
			 ti->len.t.byoyomi_stones);
	} else {
		min_playouts = stop.desired.playouts;

		/* For absolute number of simulations, slaves still
		 * use their own -t =NUM parameter. (The master
		 * needs to know the total number of simulations over
		 * all slaves so it has a different -t parameter.) */
		snprintf(args, sizeof(args), "%s\n", stone2str(color));
	}

	pthread_mutex_lock(&slave_lock);
	new_cmd(b, cmd, args);

	get_replies(time_limit, min_playouts, b);
	int replies = reply_count;

	int playouts, threads;
	dist->my_last_move.color = color;
	dist->my_last_move.coord = select_best_move(b, &dist->my_last_stats, &playouts, &threads);

	/* Tell the slaves to commit to the selected move, overwriting
	 * the last "pachi-genmoves" in the command history. */
	char *coord = coord2str(dist->my_last_move.coord, b);
	snprintf(args, sizeof(args), "%s %s\n", stone2str(color), coord);
	update_cmd(b, "play", args);
	pthread_mutex_unlock(&slave_lock);

	if (DEBUGL(1)) {
		char buf[BSIZE];
		enum stone color = dist->my_last_move.color;
		double time = time_now() - start + 0.000001; /* avoid divide by zero */
		snprintf(buf, sizeof(buf),
			 "GLOBAL WINNER is %s %s with score %1.4f (%d/%d games)\n"
			 "genmove in %0.2fs %d slaves %d threads (%d games/s,"
			 " %d games/s/slave, %d games/s/thread)\n",
			 stone2str(color), coord, get_value(dist->my_last_stats.value, color),
			 dist->my_last_stats.playouts, playouts, time, replies, threads,
			 (int)(playouts/time), (int)(playouts/time/replies),
			 (int)(playouts/time/threads));
		logline(NULL, "* ", buf);
	}
	free(coord);
	return coord_copy(dist->my_last_move.coord);
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
	get_replies(time_now() + MAX_FAST_CMD_WAIT, 0, b);

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
	struct distributed *dist = calloc(1, sizeof(struct distributed));

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

	gtp_replies = calloc(dist->max_slaves, sizeof(char *));

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
	struct engine *e = calloc(1, sizeof(struct engine));
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
