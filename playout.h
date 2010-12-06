#ifndef ZZGO_PLAYOUT_H
#define ZZGO_PLAYOUT_H

#define MAX_GAMELEN 600

struct board;
struct move;
enum stone;
struct prior_map;
struct board_ownermap;


/** Playout policy interface: */

struct playout_policy;
struct playout_setup;

/* Initialize policy data structures for new playout; subsequent choose calls
 * (but not assess/permit calls!) will all be made on the same board; if
 * setboard is used, it is guaranteed that choose will pick all moves played
 * on the board subsequently. The routine is expected to initialize b->ps
 * with internal data. At the playout end, b->ps will be simply free()d,
 * so make sure all data is within single allocated block. */
typedef void (*playoutp_setboard)(struct playout_policy *playout_policy, struct board *b);

/* Pick the next playout simulation move. */
typedef coord_t (*playoutp_choose)(struct playout_policy *playout_policy, struct playout_setup *playout_setup, struct board *b, enum stone to_play);

/* Set number of won (>0) or lost (<0) games for each considerable
 * move (usually a proportion of @games); can leave some untouched
 * if policy has no opinion. The number must have proper parity;
 * just use uct/prior.h:add_prior_value(). */
typedef void (*playoutp_assess)(struct playout_policy *playout_policy, struct prior_map *map, int games);

/* Allow play of randomly selected move. */
typedef bool (*playoutp_permit)(struct playout_policy *playout_policy, struct board *b, struct move *m);

/* Tear down the policy state; policy and policy->data will be free()d by caller. */
typedef void (*playoutp_done)(struct playout_policy *playout_policy);

struct playout_policy {
	int debug_level;
	/* We call setboard when we start new playout.
	 * We call choose when we ask policy about next move.
	 * We call assess when we ask policy about how good given move is.
	 * We call permit when we ask policy if we can make a randomly chosen move. */
	playoutp_setboard setboard;
	playoutp_choose choose;
	playoutp_assess assess;
	playoutp_permit permit;
	playoutp_done done;
	/* Particular playout policy's internal data. */
	void *data;
};


/** Playout engine interface: */

struct playout_setup {
	unsigned int gamelen; /* Maximal # of moves in playout. */
	/* Minimal difference between captures to terminate the playout.
	 * 0 means don't check. */
	unsigned int mercymin;

	/* XXX: We used to have more, perhaps we will again have more
	 * in the future. */
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
	unsigned int gamelen;
	/* Our current position in the game sequence; in AMAF, we search
	 * the range [game_baselen, gamelen]. */
	unsigned int game_baselen;

	/* Whether to record the nakade moves (true) or just completely
	 * ignore them (false; just the first color on the intersection
	 * is stored in the map, nakade counter is not incremented; game
	 * record is still kept). */
	bool record_nakade;
};


/* >0: starting_color wins, <0: starting_color loses; the actual
 * number is a DOUBLE of the score difference
 * 0: superko inside the game tree (XXX: jigo not handled) */
int play_random_game(struct playout_setup *setup,
                     struct board *b, enum stone starting_color,
                     struct playout_amafmap *amafmap,
		     struct board_ownermap *ownermap,
		     struct playout_policy *policy);

#endif
