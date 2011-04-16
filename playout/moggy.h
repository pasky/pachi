#ifndef PACHI_PLAYOUT_MOGGY_H
#define PACHI_PLAYOUT_MOGGY_H

struct board;
struct playout_policy;
struct joseki_dict;

struct playout_policy *playout_moggy_init(char *arg, struct board *b, struct joseki_dict *jdict);

#endif
