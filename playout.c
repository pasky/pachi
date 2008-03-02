#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"

int
play_random_game(struct board *b, struct move *m, int gamelen)
{
	if (b->superko_violation) {
		if (DEBUGL(0)) {
			fprintf(stderr, "\tILLEGAL: superko violation at root!\n");
			board_print(b, stderr);
		}
		return -1;
	}

	struct board b2;
	board_copy(&b2, b);

	board_play_random(&b2, m->color, &m->coord);
	if (!is_pass(m->coord) && !group_at(&b2, m->coord)) {
		if (DEBUGL(4)) {
			fprintf(stderr, "SUICIDE DETECTED at %d,%d:\n", coord_x(m->coord, b), coord_y(m->coord, b));
			board_print(&b2, stderr);
		}
		board_done_noalloc(&b2);
		return -3;
	}

	if (DEBUGL(3))
		fprintf(stderr, "[%d,%d] playing random game\n", coord_x(m->coord, b), coord_y(m->coord, b));

	gamelen = gamelen - b2.moves;
	if (gamelen < 10)
		gamelen = 10;

	enum stone color = stone_other(m->color);
	coord_t urgent;

	int passes = is_pass(m->coord);

	/* Special check: We probably tenukied the last opponent's move. But
	 * check if the opponent has lucrative local continuation for her last
	 * move! */
	/* This check is ultra-important BTW. Without it domain checking does
	 * not bring that much of an advantage. It might even warrant it to by
	 * default do only this domain check. */
	urgent = pass;
	/* domain_hint(mc, b, &urgent, m->color); */
	if (!is_pass(urgent))
		goto play_urgent;

	while (gamelen-- && passes < 2) {
		urgent = pass;
		/* domain_hint(mc, &b2, &urgent, m->color); */

		coord_t coord;

		if (!is_pass(urgent)) {
			struct move m;
play_urgent:
			m.coord = urgent; m.color = color;
			if (board_play(&b2, &m) < 0) {
				if (DEBUGL(7)) {
					fprintf(stderr, "Urgent move %d,%d is ILLEGAL:\n", coord_x(urgent, b), coord_y(urgent, b));
					board_print(&b2, stderr);
				}
				goto play_random;
			}
			coord = urgent;
		} else {
play_random:
			board_play_random(&b2, color, &coord);
		}

		if (unlikely(b2.superko_violation)) {
			/* We ignore superko violations that are suicides. These
			 * are common only at the end of the game and are
			 * rather harmless. (They will not go through as a root
			 * move anyway.) */
			if (group_at(&b2, coord)) {
				if (DEBUGL(3)) {
					fprintf(stderr, "Superko fun at %d,%d in\n", coord_x(coord, b), coord_y(coord, b));
					if (DEBUGL(4))
						board_print(&b2, stderr);
				}
				board_done_noalloc(&b2);
				return -2;
			} else {
				if (DEBUGL(6)) {
					fprintf(stderr, "Ignoring superko at %d,%d in\n", coord_x(coord, b), coord_y(coord, b));
					board_print(&b2, stderr);
				}
				b2.superko_violation = false;
			}
		}

		if (DEBUGL(7)) {
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

	float score = board_fast_score(&b2);

	board_done_noalloc(&b2);
	return (m->color == S_WHITE ? (score > 0 ? 1 : 0) : (score < 0 ? 1 : 0));
}
