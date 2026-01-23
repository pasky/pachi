#ifndef PACHI_PLAYOUT_H
#define PACHI_PLAYOUT_H

#define MAX_GAMELEN 600

#include "board.h"
#include "ownermap.h"

struct prior_map;

typedef struct playout_policy playout_policy_t;
typedef struct playout_setup playout_setup_t;


/** Playout policy interface: */

/* Initialize policy data structures for new playout; subsequent choose calls
 * (but not permit calls!) will all be made on the same board; if setboard
 * is used, it is guaranteed that choose will pick all moves played on the
 * board subsequently. The routine is expected to initialize b->ps with
 * internal data. b->ps will be simply free()d when board is destroyed,
 * so make sure all data is within single allocated block. */
typedef void (*playoutp_setboard)(playout_policy_t *playout_policy, board_t *b);

/* Pick the next playout simulation move. */
typedef coord_t (*playoutp_choose)(playout_policy_t *playout_policy, playout_setup_t *playout_setup, board_t *b, enum stone to_play);


/* Whether to allow given move. All playout moves must pass permit() before being played.
 * @alt:  policy may suggest another move if this one doesn't pass (in which case m will be changed).
 * @rnd:  move has been randomly picked.   */
typedef bool (*playoutp_permit)(playout_policy_t *playout_policy, board_t *b, move_t *m, bool alt, bool rnd);

/* Tear down the policy state; policy and policy->data will be free()d by caller. */
typedef void (*playoutp_done)(playout_policy_t *playout_policy);


struct playout_policy {
	/* We call setboard when we start new playout.
	 * We call choose when we ask policy about next move.
	 * We call permit when we ask policy if we can make a randomly chosen move. */
	playoutp_setboard setboard;
	playoutp_choose choose;
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

struct playout_setup {
	int gamelen; /* Maximal # of moves in playout. */
	/* Minimal difference between captures to terminate the playout.
	 * 0 means don't check. */
	int mercymin;
};

#define playout_setup(gamelen, mercymin)  { gamelen, mercymin }

typedef struct {
	playout_setup_t  *setup;
	playout_policy_t *policy;
} playout_t;


/* We keep record of the game so that we can examine nakade moves; really going
 * out of our way to implement nakade AMAF properly turns out to be crucial when
 * reading some tactical positions in depth (even if they are just one-stone
 * snapback). */
typedef struct {
	int gamelen;
	int game_baselen;		/* Start of the playout part of the record.
					 * in AMAF, we search the range [game_baselen, gamelen[ */
	coord_t game[MAX_GAMELEN];	/* Playout record */
	int    flags[MAX_GAMELEN];	/* Move flags */
} amafmap_t;

/* amafmap move flags */
#define AMAF_KO_CAPTURE 1

#define amaf_is_ko_capture(map, index)  ((map)->flags[index] & AMAF_KO_CAPTURE)


/* AMAF first play data */
typedef struct {
	int data[BOARD_MAX_COORDS + 1];  /* coords + pass */
} first_play_t;


void amaf_init(amafmap_t *map);

/* Record last move in amafmap. */
void amaf_record_move(amafmap_t *amaf, board_t *b);

/* Find first play at each coord in amaf record:
 *    int *first_play = amafmap_first_play(map, b, fp);
 * For each coord + pass, first_play[coord] is the map index of the first play at this
 * coordinate in the playout, or INT_MAX if the move was not played.
 * Only the playout part of the map is considered (map->game_baselen to the end). */
int *amaf_first_play(amafmap_t *map, board_t *b, first_play_t *fp);

/* Return the length of the current ko in the amaf record
 * (number of moves up to to the last ko capture). */
int amaf_ko_length(amafmap_t *map, int move);

/* Run one simulation and return score from white's perspective:
 *   >0: white wins
 *   <0: black wins
 *    0: jigo (shouldn't happen with komi)
 * Note: superko not checked during playouts. */
floating_t playout_play_game(playout_t *playout, board_t *b, enum stone starting_color,
			     amafmap_t *amafmap, ownermap_t *ownermap);

/* Get move from playout policy, or a randomly picked move if there was none. */
coord_t playout_get_move(playout_t *playout, board_t *b, enum stone color);

/* Get + play move returned by playout policy, or a randomly picked move if there was none. */
coord_t playout_play_move(playout_t *playout, board_t *b, enum stone color);

/* Is *this* move permitted ? 
 * Called by policy permit() to check something so never the main permit() call. */
bool playout_permit(playout_policy_t *p, board_t *b, coord_t coord, enum stone color, bool rnd);

void playout_policy_done(playout_policy_t *p);


#endif
