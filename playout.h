#ifndef PACHI_PLAYOUT_H
#define PACHI_PLAYOUT_H

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
	/* By default, with setboard set we will refuse to make (random)
	 * moves outside of the *choose routine in order not to mess up
	 * state tracking. If you use *setboard but do not track state
	 * (e.g. you just initialize some per-playout data, like the Moggy
	 * policy), set setboard_randomok too. */
	bool setboard_randomok;
	/* Particular playout policy's internal data. */
	void *data;
};


/** Playout engine interface: */

/* Engine hook for forcing moves before doing policy decision.
 * Return pass to forward to policy. */
typedef coord_t (*playouth_prepolicy)(struct playout_policy *playout_policy, struct playout_setup *setup, struct board *b, enum stone color);

/* Engine hook for choosing moves in case policy did not choose
 * a move.
 * Return pass to forward to uniformly random selection. */
typedef coord_t (*playouth_postpolicy)(struct playout_policy *playout_policy, struct playout_setup *setup, struct board *b, enum stone color);

struct playout_setup {
	unsigned int gamelen; /* Maximal # of moves in playout. */
	/* Minimal difference between captures to terminate the playout.
	 * 0 means don't check. */
	int mercymin;

	void *hook_data; // for hook to reference its state
	playouth_prepolicy prepolicy_hook;
	playouth_postpolicy postpolicy_hook;
};


struct playout_amafmap {
	/* We keep record of the game so that we can
	 * examine nakade moves; really going out of our way to
	 * implement nakade AMAF properly turns out to be crucial
	 * when reading some tactical positions in depth (even if
	 * they are just one-stone-snapback). */
	coord_t game[MAX_GAMELEN];
	bool is_ko_capture[MAX_GAMELEN];
	int gamelen;
	/* Our current position in the game sequence; in AMAF, we search
	 * the range [game_baselen, gamelen[ */
	int game_baselen;
};


/* >0: starting_color wins, <0: starting_color loses; the actual
 * number is a DOUBLE of the score difference
 * 0: superko inside the game tree (XXX: jigo not handled) */
int play_random_game(struct playout_setup *setup,
                     struct board *b, enum stone starting_color,
                     struct playout_amafmap *amafmap,
		     struct board_ownermap *ownermap,
		     struct playout_policy *policy);

coord_t play_random_move(struct playout_setup *setup,
		         struct board *b, enum stone color,
		         struct playout_policy *policy);

#endif
