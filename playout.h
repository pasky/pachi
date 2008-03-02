#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

struct board;
struct move;

/* 1: m->color wins, 0: m->color loses
 * -1 superko at the root
 * -2 superko inside the game tree (NOT at root, that's simply invalid move)
 * -3 first move is multi-stone suicide */
int play_random_game(struct board *b, struct move *m, int gamelen);

#endif
