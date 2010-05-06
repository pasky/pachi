#ifndef ZZGO_DISTRIBUTED_PROTOCOL_H
#define ZZGO_DISTRIBUTED_PROTOCOL_H

#include <sys/socket.h>
#include <arpa/inet.h>

#include "board.h"

void protocol_lock(void);
void protocol_unlock(void);

void logline(struct in_addr *client, char *prefix, char *s);

void update_cmd(struct board *b, char *cmd, char *args, bool new_id);
void new_cmd(struct board *b, char *cmd, char *args);
void get_replies(double time_limit);
void protocol_init(char *slave_port, char *proxy_port, int max_slaves);

extern int reply_count;
extern char **gtp_replies;
extern int active_slaves;

/* Max size of all gtp commands for one game.
 * 60 chars for the first line of genmoves plus 100 lines
 * of 30 chars each for the stats at last move. */
#define CMDS_SIZE (60*MAX_GAMELEN + 30*100)

/* Max size for one line of reply or slave log. */
#define BSIZE 4096

#endif
