#ifndef ZZGO_PLAYOUT_ELO_H
#define ZZGO_PLAYOUT_ELO_H

struct board;
struct playout_policy;

struct playout_policy *playout_elo_init(char *arg, struct board *b);

#endif
