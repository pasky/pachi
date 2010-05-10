#ifndef ZZGO_DISTRIBUTED_PROTOCOL_H
#define ZZGO_DISTRIBUTED_PROTOCOL_H

#include <sys/socket.h>
#include <arpa/inet.h>

#include "board.h"


/* Each slave thread maintains a ring of 32 buffers holding
 * incremental stats received from the slave. The oldest
 * buffer is recycled to hold stats sent to the slave and
 * received the next reply. */
#define BUFFERS_PER_SLAVE_BITS 5
#define BUFFERS_PER_SLAVE (1 << BUFFERS_PER_SLAVE_BITS)

struct slave_state;
typedef void (*buffer_hook)(void *buf, int size);
typedef void (*state_alloc_hook)(struct slave_state *sstate);
typedef int (*getargs_hook)(void *buf, struct slave_state *sstate, int cmd_id);

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

	struct {
		void *buf;
		int size;
		/* Index in received_queue, -1 if not there. */
		int queue_index;
	} b[BUFFERS_PER_SLAVE];

	int newest_buf;
	int slave_sock;

	/* Id of gtp command at time of last_processed. */
	int last_cmd_id;

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

void update_cmd(struct board *b, char *cmd, char *args, bool new_id);
void new_cmd(struct board *b, char *cmd, char *args);
void get_replies(double time_limit, int min_replies);
void protocol_init(char *slave_port, char *proxy_port, int max_slaves);

extern int reply_count;
extern char **gtp_replies;
extern int active_slaves;

/* All binary buffers received from all slaves in current move are in
 * receive_queue[0..queue_length-1] */
struct receive_buf {
	volatile void *buf;
	/* All buffers have the same physical size.
	 * size is the number of valid bytes. */
	int size;
	/* id of the thread that received the buffer. */
	int thread_id;
};
extern struct receive_buf *receive_queue;
extern int queue_length;

/* Max size of all gtp commands for one game.
 * 60 chars for the first line of genmoves plus 100 lines
 * of 30 chars each for the stats at last move. */
#define CMDS_SIZE (60*MAX_GAMELEN + 30*100)

/* Max size for one line of reply or slave log. */
#define BSIZE 4096

#endif
