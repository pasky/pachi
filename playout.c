#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"

int
play_random_game(struct board *b, enum stone starting_color, int gamelen,
		 playout_policeman policeman, void *policy)
{
	gamelen = gamelen - b->moves;
	if (gamelen < 10)
		gamelen = 10;

	enum stone color = starting_color;
	enum stone policy_color = stone_other(starting_color);
	coord_t urgent;

	int passes = is_pass(b->last_move.coord);

	while (gamelen-- && passes < 2) {
		urgent = policeman(policy, b, policy_color);

		coord_t coord;

		if (!is_pass(urgent)) {
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
		} else {
play_random:
			board_play_random(b, color, &coord);
		}

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
				return -2;
			} else {
				if (DEBUGL(6)) {
					fprintf(stderr, "Ignoring superko at %d,%d in\n", coord_x(coord, b), coord_y(coord, b));
					board_print(b, stderr);
				}
				b->superko_violation = false;
			}
		}

		if (DEBUGL(8)) {
			char *cs = coord2str(coord, b);
			fprintf(stderr, "%s %s\n", stone2str(color), cs);
			free(cs);
		}

		if (unlikely(is_pass(coord))) {
			passes++;
		} else {
			passes = 0;
		}

		color = stone_other(color);
	}

	float score = board_fast_score(b);
	bool result = (starting_color == S_WHITE ? (score > 0) : (score < 0));

	if (DEBUGL(6)) {
		fprintf(stderr, "Random playout result: %d (W %f)\n", result, score);
		if (DEBUGL(7))
			board_print(b, stderr);
	}

	return result;
}
