/* The functions implementing the master-slave protocol of the
 * distributed engine are grouped here. They are independent
 * of the gtp protocol. See the comments at the top of distributed.c
 * for a general introduction to the distributed engine. */

/* The receive queue is an array of pointers to binary buffers.
 * These pointers are invalidated in one of two ways when a buffer
 * is recycled: (1) the queue age is increased when the queue is
 * emptied at a new move, (2) the pointer itself is set to NULL
 * immmediately, and stays so until at least the next queue age
 * increment. */

#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>

#define DEBUG

#include "random.h"
#include "timeinfo.h"
#include "playout.h"
#include "network.h"
#include "debug.h"
#include "distributed/distributed.h"
#include "distributed/protocol.h"

/* All gtp commands for current game separated by \n */
static char gtp_cmds[CMDS_SIZE];

/* Latest gtp command sent to slaves. */
static char *gtp_cmd = NULL;

/* Slaves send gtp_cmd when cmd_count changes. */
static int cmd_count = 0;

/* Remember at most 10 gtp ids per move: kgs-rules, boardsize, clear_board,
 * time_settings, komi, handicap, genmoves, play pass, play pass, final_status_list */
#define MAX_CMDS_PER_MOVE 10

/* History of gtp commands sent for current game, indexed by move. */
static struct cmd_history {
	int gtp_id;
	char *next_cmd;
} history[MAX_GAMELEN][MAX_CMDS_PER_MOVE];

/* Number of active slave machines working for this master. */
int active_slaves = 0;

/* Number of replies to last gtp command already received. */
int reply_count = 0;

/* All replies to latest gtp command are in gtp_replies[0..reply_count-1]. */
char **gtp_replies;


struct buf_state **receive_queue;
int queue_length = 0;
int queue_age = 0;
static int queue_max_length;

/* Mutex protecting all variables above. receive_queue may be
 * read without the lock but is only written with lock held. */
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

/* Default slave state. */
struct slave_state default_sstate;


/* Get exclusive access to the threads and commands state. */
void
protocol_lock(void)
{
	pthread_mutex_lock(&slave_lock);
}

/* Release exclusive access to the threads and commands state. */
void
protocol_unlock(void)
{
	pthread_mutex_unlock(&slave_lock);
}

/* Write the time, client address, prefix, and string s to stderr atomically.
 * s should end with a \n */
void
logline(struct in_addr *client, char *prefix, char *s)
{
	double now = time_now();

	char addr[INET_ADDRSTRLEN];
	if (client) {
#ifdef _WIN32
		strcpy(addr, inet_ntoa(*client));
#else
		inet_ntop(AF_INET, client, addr, sizeof(addr));
#endif
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
 * The ascii reply ends with an empty line; if the first line
 * contains "@size", a binary reply of size bytes follows the
 * empty line. @size is not standard gtp, it is only used
 * internally by Pachi for the genmoves command; it must be the
 * last parameter on the line.
 * *bin_size is the maximum size upon entry, actual size on return.
 * slave_lock is not held on either entry or exit of this function. */
static int
get_reply(FILE *f, struct in_addr client, char *reply, void *bin_reply, int *bin_size)
{
	double start = time_now();

	int reply_id = -1;
	*reply = '\0';
	if (!fgets(reply, CMDS_SIZE, f)) return -1;

	/* Check for binary reply. */
	char *s = strchr(reply, '@');
	int size = 0;
	if (s) size = atoi(s+1);
	assert(size <= *bin_size);
	*bin_size = size;

	if (DEBUGV(s, 2))
		logline(&client, "<<", reply);
	if ((*reply == '=' || *reply == '?') && isdigit(reply[1]))
		reply_id = atoi(reply+1);

	/* Read the rest of the ascii reply */
	char *line = reply + strlen(reply);
	while (fgets(line, reply + CMDS_SIZE - line, f) && *line != '\n') {
		if (DEBUGL(3))
			logline(&client, "<<", line);
		line += strlen(line);
	}
	if (*line != '\n') return -1;

	/* Read the binary reply if any. */
	int len;
	while (size && (len = fread(bin_reply, 1, size, f)) > 0) {
		bin_reply = (char *)bin_reply + len;
		size -= len;
	}
	if (*bin_size && DEBUGVV(2)) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "read reply %d+%d bytes in %.4fms\n",
			 (int)strlen(reply), *bin_size,
			 (time_now() - start)*1000);
		logline(&client, "= ", buf);
	}
	return size ? -1 : reply_id;
}

/* Send the gtp command to_send and get a reply from the slave machine.
 * Write the reply in buf which must have at least CMDS_SIZE bytes.
 * If *bin_size > 0, send bin_buf after the gtp command.
 * Return any binary reply in bin_buf and set its size in bin_size.
 * bin_buf is private to the slave and need not be copied.
 * Return the gtp command id, or -1 if error.
 * slave_lock is held on both entry and exit of this function. */
static int
send_command(char *to_send, void *bin_buf, int *bin_size,
	     FILE *f, struct slave_state *sstate, char *buf)
{
	assert(to_send && gtp_cmd && bin_buf && bin_size);
	strncpy(buf, to_send, CMDS_SIZE);
	bool resend = to_send != gtp_cmd;

	pthread_mutex_unlock(&slave_lock);

	if (DEBUGL(1) && resend)
		logline(&sstate->client, "? ",
			to_send == gtp_cmds ? "resend all\n" : "partial resend\n");

	double start = time_now();
	fputs(buf, f);

	if (*bin_size)
		fwrite(bin_buf, 1, *bin_size, f);
	fflush(f);

	if (DEBUGV(strchr(buf, '@'), 2)) {
		double ms = (time_now() - start) * 1000.0;
		if (!DEBUGL(3)) {
			char *s = strchr(buf, '\n');
			if (s) s[1] = '\0';
		}
		logline(&sstate->client, ">>", buf);
		if (*bin_size) {
			char b[1024];
			snprintf(b, sizeof(b),
				 "sent cmd %d+%d bytes in %.4fms\n",
				 (int)strlen(buf), *bin_size, ms);
			logline(&sstate->client, "= ", b);
		}
	}

	/* Reuse the buffers for the reply. */
	*bin_size = sstate->max_buf_size;
	int reply_id = get_reply(f, sstate->client, buf, bin_buf, bin_size);

	pthread_mutex_lock(&slave_lock);
	return reply_id;
}

/* Return the command sent after that with the given gtp id,
 * or gtp_cmds if the id wasn't used in this game. If a play command
 * has overwritten a genmoves command, return the play command.
 * slave_lock is held on both entry and exit of this function. */
static char *
next_command(int cmd_id)
{
	if (cmd_id == -1) return gtp_cmds;

	int last_id = atoi(gtp_cmd);
	int reply_move = move_number(cmd_id);
	if (reply_move > move_number(last_id)) return gtp_cmds;

	int slot;
	for (slot = 0; slot < MAX_CMDS_PER_MOVE; slot++) {
		if (cmd_id == history[reply_move][slot].gtp_id) break;
	}
	if (slot == MAX_CMDS_PER_MOVE) return gtp_cmds;

	char *next = history[reply_move][slot].next_cmd;
	assert(next);
	return next;
}

/* Allocate buffers for a slave thread. The state should have been
 * initialized already as a copy of the default slave state.
 * slave_lock is not held on either entry or exit of this function. */
static void
slave_state_alloc(struct slave_state *sstate)
{
	for (int n = 0; n < BUFFERS_PER_SLAVE; n++) {
		sstate->b[n].buf = malloc2(sstate->max_buf_size);
		sstate->b[n].owner = sstate->thread_id;
	}
	if (sstate->alloc_hook) sstate->alloc_hook(sstate);
}

/* Get a free binary buffer, first invalidating it in the receive
 * queue if necessary. In practice all buffers should be used
 * before they are invalidated, if BUFFERS_PER_SLAVE is large enough.
 * slave_lock is held on both entry and exit of this function. */
static void *
get_free_buf(struct slave_state *sstate)
{
	int newest = (sstate->newest_buf + 1) & (BUFFERS_PER_SLAVE - 1);
	sstate->newest_buf = newest;
	void *buf = sstate->b[newest].buf;

	if (DEBUGVV(7)) {
		char b[1024];
		snprintf(b, sizeof(b),
			 "get free %d index %d buf=%p age %d qlength %d\n", newest,
			 sstate->b[newest].queue_index, buf, queue_age, queue_length);
		logline(&sstate->client, "? ", b);
	}

	int index = sstate->b[newest].queue_index;
	if (index < 0) return buf;

	/* Invalidate the buffer if the calling thread still owns its previous
	 * entry in the receive queue. The entry may have been overwritten by
	 * another thread, but only after a new move which invalidates the
	 * entire receive queue. */
	if (receive_queue[index] && receive_queue[index]->owner == sstate->thread_id) {
		receive_queue[index] = NULL;
	}
	sstate->b[newest].queue_index = -1;
	return buf;
}

/* Insert a buffer in the receive queue. It should be the most
 * recent buffer allocated by the calling thread.
 * slave_lock is held on both entry and exit of this function. */
static void
insert_buf(struct slave_state *sstate, void *buf, int size)
{
	assert(queue_length < queue_max_length);

	int newest = sstate->newest_buf;
	assert(buf == sstate->b[newest].buf);

	/* Update the buffer if necessary before making it
	 * available to other threads. */
	if (sstate->insert_hook) sstate->insert_hook(buf, size);

	if (DEBUGVV(7)) {
		char b[1024];
		snprintf(b, sizeof(b),
			 "insert newest %d age %d rq[%d]->%p owner %d\n",
			 newest, queue_age, queue_length, buf, sstate->thread_id);
			logline(&sstate->client, "? ", b);
	}
	receive_queue[queue_length] = &sstate->b[newest];
	receive_queue[queue_length]->size = size;
	receive_queue[queue_length]->queue_index = queue_length;
	queue_length++;
}

/* Clear the receive queue. The buffer pointers do not have to be cleared
 * here, this is done as each buffer is recycled.
 * slave_lock is held on both entry and exit of this function. */
void
clear_receive_queue(void)
{
	if (DEBUGL(3)) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "clear queue, old length %d age %d\n",
			 queue_length, queue_age);
		logline(NULL, "? ", buf);
	}
	queue_length = 0;
	queue_age++;
}

/* Process the reply received from a slave machine.
 * Copy the ascii part to reply_buf and insert the binary part
 * (if any) in the receive queue.
 * Return false if ok, true if the slave is out of sync.
 * slave_lock is held on both entry and exit of this function. */
static bool
process_reply(int reply_id, char *reply, char *reply_buf,
	      void *bin_reply, int bin_size, int *last_reply_id,
	      int *reply_slot, struct slave_state *sstate)
{
	/* Resend everything if slave returned an error. */
	if (*reply != '=') {
		*last_reply_id = -1;
		return true;
	}
	/* Make sure we are still in sync. cmd_count may have
	 * changed but the reply is valid as long as cmd_id didn't
	 * change (this only occurs for consecutive genmoves). */
	int cmd_id = atoi(gtp_cmd);
	if (reply_id != cmd_id) {
		*last_reply_id = reply_id;
		return true;
	}

	strncpy(reply_buf, reply, CMDS_SIZE);
	if (reply_id != *last_reply_id)
		*reply_slot = reply_count++;
	gtp_replies[*reply_slot] = reply_buf;

	if (bin_size) insert_buf(sstate, bin_reply, bin_size);

	pthread_cond_signal(&reply_cond);
	*last_reply_id = reply_id;
	return false;
}

/* Get the binary arg for the given command, and update the command
 * if necessary. For now, only genmoves has a binary argument, and
 * we return the best stats increments from all other slaves.
 * Set *bin_size to 0 if the command doesn't take binary arguments,
 * but still return a buffer, to be used for the reply.
 * Return NULL if the binary arg is obsolete by the time we have
 * finished computing it, because a new command is available.
 * This version only gets the buffer for the reply, to be completed
 * in future commits.
 * slave_lock is held on both entry and exit of this function. */
void *
get_binary_arg(struct slave_state *sstate, char *cmd, int cmd_size, int *bin_size)
{
	int cmd_id = atoi(gtp_cmd);
	void *buf = get_free_buf(sstate);

	*bin_size = 0;
	char *s = strchr(cmd, '@');
	if (!s || !sstate->args_hook) return buf;

	int size = sstate->args_hook(buf, sstate, cmd_id);

	/* Check that the command is still valid. */
	if (atoi(gtp_cmd) != cmd_id) return NULL;

	/* Set the correct binary size for this slave.
	 * cmd may have been overwritten with new parameters. */
	*bin_size = size;
	s = strchr(cmd, '@');
	assert(s);
	snprintf(s, cmd + cmd_size - s, "@%d\n", size);
	return buf;
}

/* Main loop of a slave thread.
 * Send the current command to the slave machine and wait for a reply.
 * Resend command history if the slave machine is out of sync.
 * Returns when the connection with the slave machine is cut.
 * slave_lock is held on both entry and exit of this function. */
static void
slave_loop(FILE *f, char *reply_buf, struct slave_state *sstate, bool resend)
{
	char *to_send;
	int last_cmd_count = 0;
	int last_reply_id = -1;
	int reply_slot = -1;
	for (;;) {
		if (resend) {
			/* Resend complete or partial history */
			to_send = next_command(last_reply_id);
		} else {
			/* Wait for a new command. */
			while (last_cmd_count == cmd_count)
				pthread_cond_wait(&cmd_cond, &slave_lock);
			to_send = gtp_cmd;
		}

		/* Command available, send it to slave machine.
		 * If slave was out of sync, send the history.
		 * But first get binary arguments if necessary. */
		int bin_size = 0;
		void *bin_buf = get_binary_arg(sstate, gtp_cmd,
					       gtp_cmds + CMDS_SIZE - gtp_cmd,
					       &bin_size);
		/* Check that the command is still valid. */
		resend = true;
		if (!bin_buf) continue;

		/* Send the command and get the reply, which always ends with \n\n
		 * The slave machine sends "=id reply" or "?id reply"
		 * with id == cmd_id if it is in sync. */
		last_cmd_count = cmd_count;
		char buf[CMDS_SIZE];
		int reply_id = send_command(to_send, bin_buf, &bin_size, f,
					    sstate, buf);
		if (reply_id == -1) return;

		resend = process_reply(reply_id, buf, reply_buf, bin_buf, bin_size,
				       &last_reply_id, &reply_slot, sstate);
	}
}

/* Minimimal check of slave identity. Close the file if error. */
static bool
is_pachi_slave(FILE *f, struct in_addr *client)
{
	char buf[1024];
	fputs("name\n", f);
	if (!fgets(buf, sizeof(buf), f)
	    || strncasecmp(buf, "= Pachi", 7)
	    || !fgets(buf, sizeof(buf), f)
	    || strcmp(buf, "\n")) {
		logline(client, "? ", "bad slave\n");
		fclose(f);
		sleep(1); // avoid busy loop if error
		return false;
	}
	return true;
}

/* Thread sending gtp commands to one slave machine, and
 * reading replies. If a slave machine dies, this thread waits
 * for a connection from another slave.
 * The large buffers are allocated only once we get a first
 * connection, to avoid wasting memory if max_slaves is too large.
 * We do not invalidate the received buffers if a slave disconnects;
 * they are still useful for other slaves. */
static void *
slave_thread(void *arg)
{
	struct slave_state sstate = default_sstate;
	sstate.thread_id = (long)arg;

	assert(sstate.slave_sock >= 0);
	char reply_buf[CMDS_SIZE];
	bool resend = false;

	for (;;) {
		/* Wait for a connection from any slave. */
		struct in_addr client;
		int conn = open_server_connection(sstate.slave_sock, &client);

		FILE *f = fdopen(conn, "r+");
		if (DEBUGL(2)) {
			snprintf(reply_buf, sizeof(reply_buf),
				 "new slave, id %d\n", sstate.thread_id);
			logline(&client, "= ", reply_buf);
		}
		if (!is_pachi_slave(f, &client)) continue;

		if (!resend) slave_state_alloc(&sstate);
		sstate.client = client;

		pthread_mutex_lock(&slave_lock);
		active_slaves++;
		slave_loop(f, reply_buf, &sstate, resend);

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
void
update_cmd(struct board *b, char *cmd, char *args, bool new_id)
{
	assert(gtp_cmd);
	/* To make sure the slaves are in sync, we ignore the original id
	 * and use the board number plus some random bits as gtp id. */
	static int gtp_id = -1;
	int moves = is_reset(cmd) ? 0 : b->moves;
	if (new_id) {
		int prev_id = gtp_id;
		do {
			/* fast_random() is 16-bit only so the multiplication can't overflow. */
			gtp_id = force_reply(moves + fast_random(65535) * DIST_GAMELEN);
		} while (gtp_id == prev_id);
		reply_count = 0;
	}
	snprintf(gtp_cmd, gtp_cmds + CMDS_SIZE - gtp_cmd, "%d %s %s",
		 gtp_id, cmd, *args ? args : "\n");
	cmd_count++;

	/* Remember history for out-of-sync slaves. */
	static int slot = 0;
	static struct cmd_history *last = NULL;
	if (new_id) {
		if (last) last->next_cmd = gtp_cmd;
		slot = (slot + 1) % MAX_CMDS_PER_MOVE;
		last = &history[moves][slot];
		last->gtp_id = gtp_id;
		last->next_cmd = NULL;
	}
	// Notify the slave threads about the new command.
	pthread_cond_broadcast(&cmd_cond);
}

/* Update the command history, then create a new gtp command
 * for all slaves. The slave lock is held upon entry and
 * upon return, so the command will actually be sent when the
 * lock is released. cmd is a single word; args has all
 * arguments and is empty or has a trailing \n */
void
new_cmd(struct board *b, char *cmd, char *args)
{
	// Clear the history when a new game starts:
	if (!gtp_cmd || is_gamestart(cmd)) {
		gtp_cmd = gtp_cmds;
		memset(history, 0, sizeof(history));
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

/* Wait for at least one new reply. Return when at least
 * min_replies slaves have already replied, or when the
 * given absolute time is passed.
 * The replies are returned in gtp_replies[0..reply_count-1]
 * slave_lock is held on entry and on return. */
void
get_replies(double time_limit, int min_replies)
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
		if (reply_count >= min_replies || reply_count >= active_slaves) return;
		if (time_now() >= time_limit) break;
	}
	if (DEBUGL(1)) {
		char buf[1024];
		snprintf(buf, sizeof(buf),
			 "get_replies timeout %.3f >= %.3f, replies %d < min %d, active %d\n",
			 time_now() - start_time, time_limit - start_time,
			 reply_count, min_replies, active_slaves);
		logline(NULL, "? ", buf);
	}
	assert(reply_count > 0);
}

/* In a 5mn move with at least 5ms per genmoves we get at most
 * 300*200=60000 genmoves per slave. */
#define MAX_GENMOVES_PER_SLAVE 60000

/* Allocate the receive queue, and create the slave and proxy threads.
 * max_buf_size and the merge-related fields of default_sstate must
 * already be initialized. */
void
protocol_init(char *slave_port, char *proxy_port, int max_slaves)
{
	start_time = time_now();

	queue_max_length = max_slaves * MAX_GENMOVES_PER_SLAVE;
	receive_queue = calloc2(queue_max_length, sizeof(*receive_queue));

	default_sstate.slave_sock = port_listen(slave_port, max_slaves);
	default_sstate.last_processed = -1;

	for (int n = 0; n < BUFFERS_PER_SLAVE; n++) {
		default_sstate.b[n].queue_index = -1;
	}

	pthread_t thread;
	for (int id = 0; id < max_slaves; id++) {
		pthread_create(&thread, NULL, slave_thread, (void *)(long)id);
	}

	if (proxy_port) {
		int proxy_sock = port_listen(proxy_port, max_slaves);
		for (int id = 0; id < max_slaves; id++) {
			pthread_create(&thread, NULL, proxy_thread, (void *)(long)proxy_sock);
		}
	}
}
