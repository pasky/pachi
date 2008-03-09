#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

struct board;
struct move;
enum stone;


typedef coord_t (*playout_policeman)(void *playout_policy, struct board *b, coord_t last_move, enum stone my_color);


/* 1: starting_color wins, 0: starting_color loses
 * -1: superko inside the game tree */
int play_random_game(struct board *b, enum stone starting_color, int gamelen, coord_t second_to_last_coord, playout_policeman policeman, void *policy);

#endif
