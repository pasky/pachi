#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

struct board;
struct move;
enum stone;


typedef coord_t (*playout_policeman)(void *playout_policy, struct board *b, enum stone my_color);

/* Record of the random playout - for each intersection:
 * S_NONE: This move was never played
 * S_BLACK: This move was played by black first
 * S_WHITE: This move was played by white first
 */
struct playout_amafmap {
	enum stone *map; // [b->size2]
};


/* 1: starting_color wins, 0: starting_color loses
 * -1: superko inside the game tree */
int play_random_game(struct board *b, enum stone starting_color, int gamelen, struct playout_amafmap *amafmap, playout_policeman policeman, void *policy);

#endif
