#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "ownermap.h"
#include "playout.h"

/* Whether to set global debug level to the same as the playout
 * has, in case it is different. This can make sure e.g. tactical
 * reading produces proper level of debug prints during simulations.
 * But it is safe to enable this only in single-threaded instances! */
//#define DEBUGL_BY_PLAYOUT

#define PLDEBUGL(n) DEBUGL_(policy->debug_level, n)


/* Full permit logic, ie m->coord may get changed to an alternative move */
static bool
playout_permit_move(struct playout_policy *p, struct board *b, struct move *m, bool alt)
{	
	coord_t coord = m->coord;
	if (coord == pass || coord == resign)
		return false;

	if (!board_permit(b, m, NULL) ||
	    (p->permit && !p->permit(p, b, m, alt)))
		return false;

	return true;
}

/* Return coord if move is ok, an alternative move or pass if not */
static coord_t
playout_check_move(struct playout_policy *p, struct board *b, coord_t coord, enum stone color)
{
	struct move m = { .coord = coord, .color = color };
	if (!playout_permit_move(p, b, &m, 1))
		return pass;
	return m.coord;
}

/* Is *this* move permitted ? 
 * Called by policy permit() to check something so never the main permit() call. */
bool
playout_permit(struct playout_policy *p, struct board *b, coord_t coord, enum stone color)
{
	struct move m = { .coord = coord, .color = color };
	return playout_permit_move(p, b, &m, 0);
}

static bool
permit_handler(struct board *b, struct move *m, void *data)
{
	struct playout_policy *policy = data;
	return playout_permit_move(policy, b, m, 1);
}


coord_t
playout_play_move(struct playout_setup *setup,
		  struct board *b, enum stone color,
		  struct playout_policy *policy)
{
	coord_t coord = pass;
	
	if (is_pass(coord)) {
		coord = policy->choose(policy, setup, b, color);
		coord = playout_check_move(policy, b, coord, color);
		// fprintf(stderr, "policy: %s\n", coord2sstr(coord, b));
	}

	if (is_pass(coord)) {
	play_random:
		/* Defer to uniformly random move choice. */
		/* This must never happen if the policy is tracking
		 * internal board state, obviously. */
		assert(!policy->setboard || policy->setboard_randomok);
		board_play_random(b, color, &coord, permit_handler, policy);

	} else {
		struct move m;
		m.coord = coord; m.color = color;
		if (board_play(b, &m) < 0) {
			if (PLDEBUGL(4)) {
				fprintf(stderr, "Pre-picked move %d,%d is ILLEGAL:\n",
					coord_x(coord, b), coord_y(coord, b));
				board_print(b, stderr);
			}
			goto play_random;
		}
	}

	return coord;
}

int
playout_play_game(struct playout_setup *setup,
		  struct board *b, enum stone starting_color,
		  struct playout_amafmap *amafmap,
		  struct board_ownermap *ownermap,
		  struct playout_policy *policy)
{
	assert(setup && policy);

	int gamelen = setup->gamelen - b->moves;

	if (policy->setboard)
		policy->setboard(policy, b);
#ifdef DEBUGL_BY_PLAYOUT
	int debug_level_orig = debug_level;
	debug_level = policy->debug_level;
#endif

	enum stone color = starting_color;

	int passes = is_pass(b->last_move.coord) && b->moves > 0;

	while (gamelen-- && passes < 2) {
		coord_t coord = playout_play_move(setup, b, color, policy);

		if (PLDEBUGL(7)) {
			fprintf(stderr, "%s %s\n", stone2str(color), coord2sstr(coord, b));
			if (PLDEBUGL(8))  board_print(b, stderr);
		}

		if (unlikely(is_pass(coord)))  passes++;
		else                           passes = 0;
		
		if (amafmap) {
			assert(amafmap->gamelen < MAX_GAMELEN);
			amafmap->is_ko_capture[amafmap->gamelen] = board_playing_ko_threat(b);
			amafmap->game[amafmap->gamelen++] = coord;
		}

		if (setup->mercymin && abs(b->captures[S_BLACK] - b->captures[S_WHITE]) > setup->mercymin)
			break;

		color = stone_other(color);
	}

	floating_t score = board_fast_score(b);
	int result = (starting_color == S_WHITE ? score * 2 : - (score * 2));

	if (DEBUGL(6)) {
		fprintf(stderr, "Random playout result: %d (W %f)\n", result, score);
		if (DEBUGL(7))  board_print(b, stderr);
	}

	if (ownermap)  board_ownermap_fill(ownermap, b);

#ifdef DEBUGL_BY_PLAYOUT
	debug_level = debug_level_orig;
#endif

	return result;
}


void
playout_policy_done(struct playout_policy *p)
{
	if (p->done) p->done(p);
	if (p->data) free(p->data);
	free(p);
}


