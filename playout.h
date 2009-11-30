#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

#define MAX_GAMELEN 600

struct board;
struct move;
enum stone;
struct prior_map;
struct board_ownermap;


struct playout_policy;
typedef coord_t (*playoutp_choose)(struct playout_policy *playout_policy, struct board *b, enum stone to_play);
/* Set number of won (>0) or lost (<0) games for each considerable
 * move (usually a proportion of @games); can leave some untouched
 * if policy has no opinion. The number must have proper parity;
 * just use uct/prior.h:add_prior_value(). */
typedef void (*playoutp_assess)(struct playout_policy *playout_policy, struct prior_map *map, int games);
typedef bool (*playoutp_permit)(struct playout_policy *playout_policy, struct board *b, struct move *m);

struct playout_policy {
	int debug_level;
	/* We call choose when we ask policy about next move.
	 * We call assess when we ask policy about how good given move is.
	 * We call permit when we ask policy if we can make a randomly chosen move. */
	playoutp_choose choose;
	playoutp_assess assess;
	playoutp_permit permit;
	void *data;
};

struct playout_amafmap {
	/* Record of the random playout - for each intersection:
	 * S_NONE: This move was never played
	 * S_BLACK: This move was played by black first
	 * S_WHITE: This move was played by white first
	 */
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

	/* Additionally, we keep record of the game so that we can
	 * examine nakade moves; really going out of our way to
	 * implement nakade AMAF properly turns out to be crucial
	 * when reading some tactical positions in depth (even if
	 * they are just one-stone-snapback). */
	struct move game[MAX_GAMELEN + 1];
	int gamelen;
	/* Our current position in the game sequence; in AMAF, we search
	 * the range [game_baselen, gamelen]. */
	int game_baselen;

	/* Whether to record the nakade moves (true) or just completely
	 * ignore them (false; just the first color on the intersection
	 * is stored in the map, nakade counter is not incremented; game
	 * record is still kept). */
	bool record_nakade;
};


/* >0: starting_color wins, <0: starting_color loses; the actual
 * number is a DOUBLE of the score difference
 * 0: superko inside the game tree (XXX: jigo not handled) */
int play_random_game(struct board *b, enum stone starting_color, int gamelen,
                     struct playout_amafmap *amafmap,
		     struct board_ownermap *ownermap,
		     struct playout_policy *policy);

#endif
