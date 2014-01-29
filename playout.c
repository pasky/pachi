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


coord_t
play_random_move(struct playout_setup *setup,
		 struct board *b, enum stone color,
		 struct playout_policy *policy)
{
	coord_t coord = pass;

	if (setup->prepolicy_hook) {
		coord = setup->prepolicy_hook(policy, setup, b, color);
		// fprintf(stderr, "prehook: %s\n", coord2sstr(coord, b));
	}

	if (is_pass(coord)) {
		coord = policy->choose(policy, setup, b, color);
		// fprintf(stderr, "policy: %s\n", coord2sstr(coord, b));
	}

	if (is_pass(coord) && setup->postpolicy_hook) {
		coord = setup->postpolicy_hook(policy, setup, b, color);
		// fprintf(stderr, "posthook: %s\n", coord2sstr(coord, b));
	}

	if (is_pass(coord)) {
play_random:
		/* Defer to uniformly random move choice. */
		/* This must never happen if the policy is tracking
		 * internal board state, obviously. */
		assert(!policy->setboard || policy->setboard_randomok);
		board_play_random(b, color, &coord, (ppr_permit) policy->permit, policy);

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
play_random_game(struct playout_setup *setup,
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
		coord_t coord = play_random_move(setup, b, color, policy);

#if 0
		/* For UCT, superko test here is downright harmful since
		 * in superko-likely situation we throw away literally
		 * 95% of our playouts; UCT will deal with this fine by
		 * itself. */
		if (unlikely(b->superko_violation)) {
			/* We ignore superko violations that are suicides. These
			 * are common only at the end of the game and are
			 * rather harmless. (They will not go through as a root
			 * move anyway.) */
			if (group_at(b, coord)) {
				if (DEBUGL(3)) {
					fprintf(stderr, "Superko fun at %d,%d in\n", coord_x(coord, b), coord_y(coord, b));
					if (DEBUGL(4))
						board_print(b, stderr);
				}
				return 0;
			} else {
				if (DEBUGL(6)) {
					fprintf(stderr, "Ignoring superko at %d,%d in\n", coord_x(coord, b), coord_y(coord, b));
					board_print(b, stderr);
				}
				b->superko_violation = false;
			}
		}
#endif

		if (PLDEBUGL(7)) {
			fprintf(stderr, "%s %s\n", stone2str(color), coord2sstr(coord, b));
			if (PLDEBUGL(8))
				board_print(b, stderr);
		}

		if (unlikely(is_pass(coord))) {
			passes++;
		} else {
			passes = 0;
		}
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
		if (DEBUGL(7))
			board_print(b, stderr);
	}

	if (ownermap)
		board_ownermap_fill(ownermap, b);

	if (b->ps)
		free(b->ps);

#ifdef DEBUGL_BY_PLAYOUT
	debug_level = debug_level_orig;
#endif

	return result;
}
