#ifndef PACHI_PLAYOUT_LIGHT_H
#define PACHI_PLAYOUT_LIGHT_H

struct board;
struct playout_policy;

struct playout_policy *playout_light_init(char *arg, struct board *b);

#endif
