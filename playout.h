#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

struct board;
struct move;
enum stone;


typedef coord_t (*playout_policeman)(void *playout_policy, struct board *b, enum stone my_color);


/* 1: m->color wins, 0: m->color loses
 * -2 superko inside the game tree (NOT at root, that's simply invalid move)
 * -3 first move is multi-stone suicide */
int play_random_game(struct board *b, struct move *m, int gamelen, playout_policeman policeman, void *policy);

#endif
