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
#include "probdist.h"


static coord_t
try_probdist_move(struct board *b, enum stone color, struct playout_callback *callback)
{
	/* First try probability distribution provided by
	 * the engine for this move. */
	struct probdist pd;
	probdist_init(&pd, board_size2(b));

	callback->probdist(callback, &pd, b);
	if (pd.moves[0] >= pd.total - __FLT_EPSILON__) {
		probdist_done(&pd);
		return pass;
	}

	coord_t urgent = probdist_pick(&pd);
	probdist_done(&pd);

	/* If the pick is invalid, defer to policy. */
	if (!urgent || is_pass(urgent))
		return pass;
	struct move m = { .coord = urgent, .color = color };
	if (!board_is_valid_move(b, &m))
		return pass;

	return urgent;
}

int
play_random_game(struct board *b, enum stone starting_color, int gamelen,
		 struct playout_amafmap *amafmap,
		 struct board_ownermap *ownermap,
		 struct playout_policy *policy,
		 struct playout_callback *callback)
{
	gamelen = gamelen - b->moves;
	if (gamelen < 10)
		gamelen = 10;

	enum stone color = starting_color;

	int passes = is_pass(b->last_move.coord) && b->moves > 0;

	while (gamelen-- && passes < 2) {
		coord_t urgent = pass;

		if (callback && callback->probdist) {
			urgent = try_probdist_move(b, color, callback);
		}

		if (is_pass(urgent)) {
			/* Defer to policy move choice. */
			urgent = policy->choose(policy, b, color);
		}

		coord_t coord;

		if (is_pass(urgent)) {
play_random:
			/* Defer to uniformly random move choice. */
			board_play_random(b, color, &coord, (ppr_permit) policy->permit, policy);

		} else {
			struct move m;
			m.coord = urgent; m.color = color;
			if (board_play(b, &m) < 0) {
				if (DEBUGL(8)) {
					fprintf(stderr, "Urgent move %d,%d is ILLEGAL:\n", coord_x(urgent, b), coord_y(urgent, b));
					board_print(b, stderr);
				}
				goto play_random;
			}
			coord = urgent;
		}

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

		if (DEBUGL(7)) {
			fprintf(stderr, "%s %s\n", stone2str(color), coord2sstr(coord, b));
			if (DEBUGL(8))
				board_print(b, stderr);
		}

		if (unlikely(is_pass(coord))) {
			passes++;
		} else {
			/* We don't care about nakade counters, since we want
			 * to avoid taking pre-nakade moves into account only
			 * if they happenned in the tree before nakade nodes;
			 * but this is always out of the tree. */
			if (amafmap) {
				if (amafmap->map[coord] == S_NONE || amafmap->map[coord] == color)
					amafmap->map[coord] = color;
				else if (amafmap->record_nakade)
					amaf_op(amafmap->map[coord], +);
				amafmap->game[amafmap->gamelen].coord = coord;
				amafmap->game[amafmap->gamelen].color = color;
				amafmap->gamelen++;
				assert(amafmap->gamelen < sizeof(amafmap->game) / sizeof(amafmap->game[0]));
			}

			passes = 0;
		}

		color = stone_other(color);
	}

	float score = board_fast_score(b);
	int result = (starting_color == S_WHITE ? score * 2 : - (score * 2));

	if (DEBUGL(6)) {
		fprintf(stderr, "Random playout result: %d (W %f)\n", result, score);
		if (DEBUGL(7))
			board_print(b, stderr);
	}

	if (ownermap)
		board_ownermap_fill(ownermap, b);

	return result;
}
