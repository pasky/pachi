#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

struct board;
struct move;
enum stone;


struct playout_policy;
typedef coord_t (*playoutp_choose)(struct playout_policy *playout_policy, struct board *b, enum stone to_play);
/* 0.0 - 1.0; can return NAN is policy has no opinion */
typedef float (*playoutp_assess)(struct playout_policy *playout_policy, struct board *b, struct move *m);

struct playout_policy {
	int debug_level;
	/* We call choose when we ask policy about next move.
	 * We call assess when we ask policy about how good given move is. */
	playoutp_choose choose;
	playoutp_assess assess;
	void *data;
};


/* Record of the random playout - for each intersection:
 * S_NONE: This move was never played
 * S_BLACK: This move was played by black first
 * S_WHITE: This move was played by white first
 */
struct playout_amafmap {
	enum stone *map; // [board_size2(b)]
	/* the lowest &0xf is the enum stone, upper bits are nakade
	 * counter - in case of nakade, we record only color of the
	 * first stone played inside, but count further throwins
	 * and ignore AMAF value after these. */
#define amaf_nakade(item_) (item_ >> 8)
#define amaf_op(item_, op_) do { \
		int mi_ = item_; \
		item_ = (mi_ & 0xf) | ((amaf_nakade(mi_) op_ 1) << 8); \
} while (0)
};


/* 1: starting_color wins, 0: starting_color loses
 * -1: superko inside the game tree */
int play_random_game(struct board *b, enum stone starting_color, int gamelen, struct playout_amafmap *amafmap, struct playout_policy *policy);

#endif
