#ifndef PACHI_DISTRIBUTED_PROTOCOL_H
#define PACHI_DISTRIBUTED_PROTOCOL_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include "board.h"


/* Each slave thread maintains a ring of 256 buffers holding
 * incremental stats received from the slave. The oldest
 * buffer is recycled to hold stats sent to the slave and
 * received the next reply. */
#define BUFFERS_PER_SLAVE_BITS 8
#define BUFFERS_PER_SLAVE (1 << BUFFERS_PER_SLAVE_BITS)

struct slave_state;
typedef void (*buffer_hook)(void *buf, int size);
typedef void (*state_alloc_hook)(struct slave_state *sstate);
typedef int (*getargs_hook)(void *buf, struct slave_state *sstate, int cmd_id);

struct buf_state {
	void *buf;
	/* All buffers have the same physical size. size is the
	 * number of valid bytes. It is set only when the buffer
	 * is actually in the receive queueue. */
	int size;
	int queue_index;
	int owner;
};

struct slave_state {
	int max_buf_size;
	int thread_id;
	struct in_addr client; // for debugging only
	state_alloc_hook alloc_hook;
	buffer_hook insert_hook;
	getargs_hook args_hook;

	/* Index in received_queue of most recent processed
	 * buffer, -1 if none processed yet. */
	int last_processed;

	/* --- PRIVATE DATA for protocol.c --- */

	struct buf_state b[BUFFERS_PER_SLAVE];
	int newest_buf;
	int slave_sock;

	/* --- PRIVATE DATA for merge.c --- */

	/* Hash table of incremental stats. */
	struct incr_stats *stats_htable;
	int stats_hbits;
	int stats_id;

	/* Hash indices updated by stats merge. */
	int *merged;
	int max_merged_nodes;
};
extern struct slave_state default_sstate;

void protocol_lock(void);
void protocol_unlock(void);

void logline(struct in_addr *client, char *prefix, char *s);

void clear_receive_queue(void);
void update_cmd(struct board *b, char *cmd, char *args, bool new_id);
void new_cmd(struct board *b, char *cmd, char *args);
void get_replies(double time_limit, int min_replies);
void protocol_init(char *slave_port, char *proxy_port, int max_slaves);

extern int reply_count;
extern char **gtp_replies;
extern int active_slaves;

/* All binary buffers received from all slaves in current move are in
 * receive_queue[0..queue_length-1] */
extern struct buf_state **receive_queue;
extern int queue_length;
/* Queue age is incremented each time the queue is emptied. */
extern int queue_age;

/* Max size of all gtp commands for one game.
 * 60 chars for the first line of genmoves plus 100 lines
 * of 30 chars each for the stats at last move. */
#define CMDS_SIZE (60*MAX_GAMELEN + 30*100)

/* Max size for one line of reply or slave log. */
#define BSIZE 4096

#endif
