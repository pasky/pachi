#ifndef PACHI_UCT_SLAVE_H
#define PACHI_UCT_SLAVE_H

#include "move.h"
#include "distributed/distributed.h"

struct board;
struct engine;
struct time_info;

enum parse_code uct_notify(struct engine *e, struct board *b, int id, char *cmd, char *args, char **reply);
char *uct_genmoves(struct engine *e, struct board *b, struct time_info *ti, enum stone color,
		   char *args, bool pass_all_alive, void **stats_buf, int *stats_size);
void *uct_htable_alloc(int hbits);
void uct_htable_reset(struct tree *t);

#endif
