#include <stdio.h>
#include <stdlib.h>

#define DEBUG

#include "board.h"
#include "montecarlo/internal.h"
#include "random.h"


/* *** Domain-specific knowledge comes here (that is, any heuristics that perfer
 * certain moves, aside of requiring the moves to be according to the rules). */
/* NOTE: This heuristics affects ONLY the random playouts! It does not help the
 * engine directly to pick a move, but it makes it pick the hinted moves in the
 * random playouts FROM the random initial move. So the engine will not prefer
 * to fix atari on the current board, but it will fix it as the other player
 * when the next move on current board failed to deal with it. */

static bool inline
valid_escape_route(struct board *b, coord_t from, coord_t to)
{
	/* Make sure we actually gain any liberties by this escape route. */
	return neighbor_count_at(b, to, S_NONE) > 1;
}

static coord_t
domain_hint_capture(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* If we or our neighbors are in atari, fix that, (Capture or escape.)
	 * This test costs a lot of performance (the whole playout is about 1/4
	 * slower), but improves the playouts a lot. */

	if (MCDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-capture moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t captures[5]; int captures_len = 0;
	memset(captures, 0, sizeof(captures));

	coord_t fix;
	if (unlikely(board_group_in_atari(b, group_at(b, coord), &fix)) && likely(valid_escape_route(b, coord, fix)))
		captures[captures_len++] = fix;
	foreach_neighbor(b, coord, {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_in_atari(b, group_at(b, c), &fix)) && likely(valid_escape_route(b, coord, fix)))
			captures[captures_len++] = fix;
	} );

	if (unlikely(captures_len)) {
		if (MCDEBUGL(8)) {
			fprintf(stderr, "capture moves found:");
			int i = 0;
			for (i = 0; i < captures_len; i++)
				fprintf(stderr, " %d,%d", coord_x(captures[i], b), coord_y(captures[i], b));
			fprintf(stderr, "\n");
		}
		return captures[fast_random(captures_len)];
	}
	return pass;
}

static coord_t
domain_hint_atari(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* If we can put the last-move group in atari, do that. */

	if (MCDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-atari moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t ataris[5][2]; int ataris_len = 0;
	memset(ataris, 0, sizeof(ataris));

	if (unlikely(board_group_can_atari(b, group_at(b, coord), ataris[ataris_len])))
		ataris_len++;
	foreach_neighbor(b, coord, {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_can_atari(b, group_at(b, c), ataris[ataris_len])))
			ataris_len++;
	} );

	if (unlikely(ataris_len)) {
		if (MCDEBUGL(8)) {
			fprintf(stderr, "atari moves found:");
			int i = 0;
			for (i = 0; i < ataris_len; i++)
				fprintf(stderr, " %d,%d;%d,%d",
					coord_x(ataris[i][0], b), coord_y(ataris[i][0], b),
					coord_x(ataris[i][1], b), coord_y(ataris[i][1], b));
			fprintf(stderr, "\n");
		}
		return ataris[fast_random(ataris_len)][fast_random(2)];
	}
	return pass;
}

static coord_t
domain_hint_cut(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* Check if this move is cutting kosumi:
	 * (O) X
	 *  X  .  */

	if (MCDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-cut moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t cuts[4]; int cuts_len = 0;
	memset(cuts, 0, sizeof(cuts));

	enum stone cutting_color = stone_other(board_at(b, coord));
	foreach_diag_neighbor(b, coord) {
		if (board_at(b, c) == S_NONE) {
			if (neighbor_count_at(b, c, cutting_color) != 2) {
				/* Either this isn't a cut or the opponent has
				 * too many friends there. */
				continue;
			}

			/* XXX: Some internal board specific magic here... */
			coord_t cutted = coord;
			if (coord_x(c, b) < coord_x(coord, b))
				coord_raw(cutted)--;
			else
				coord_raw(cutted)++;
			if (likely(board_at(b, cutted) != cutting_color))
				continue;
			coord_raw(cutted) = coord_raw(c);
			if (coord_y(c, b) < coord_y(coord, b))
				coord_raw(cutted) -= b->size;
			else
				coord_raw(cutted) += b->size;
			if (likely(board_at(b, cutted) != cutting_color))
				continue;
			/* Cut kosumi! */
			cuts[cuts_len++] = c;
		}
	} foreach_diag_neighbor_end;

	if (unlikely(cuts_len)) {
		if (MCDEBUGL(8)) {
			fprintf(stderr, "Cutting moves found:");
			int i = 0;
			for (i = 0; i < cuts_len; i++)
				fprintf(stderr, " %d,%d", coord_x(cuts[i], b), coord_y(cuts[i], b));
			fprintf(stderr, "\n");
		}
		return cuts[fast_random(cuts_len)];
	}
	return pass;
}

static coord_t
domain_hint_local(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* Pick a suitable move that is directly or diagonally adjecent. In the
	 * real game, local moves often tend to be the urgent ones, even if they
	 * aren't atari. */

	if (MCDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-local moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t neis[S_MAX][8]; int neis_len[S_MAX];
	memset(neis, 0, sizeof(neis));
	memset(neis_len, 0, sizeof(neis_len));

	foreach_neighbor(b, coord, {
		neis[(enum stone) board_at(b, c)][neis_len[(enum stone) board_at(b, c)]++] = c;
	} );

	foreach_diag_neighbor(b, coord) {
		neis[(enum stone) board_at(b, c)][neis_len[(enum stone) board_at(b, c)]++] = c;
	} foreach_diag_neighbor_end;

	if (likely(neis_len[S_NONE])) {
		if (MCDEBUGL(8)) {
			fprintf(stderr, "Local moves found:");
			int i = 0;
			for (i = 0; i < neis_len[S_NONE]; i++)
				fprintf(stderr, " %d,%d", coord_x(neis[S_NONE][i], b), coord_y(neis[S_NONE][i], b));
			fprintf(stderr, "\n");
		}
		return neis[S_NONE][fast_random(neis_len[S_NONE])];
	}
	return pass;
}

coord_t
domain_hint(struct montecarlo *mc, struct board *b, enum stone our_real_color)
{
	if (is_pass(b->last_move.coord))
		return pass;

	/* Now now, if we ignored an urgent move, the opponent will
	 * take it! */
	/* Note that we should use this only when the _REAL_ us tenukies
	 * and the _REAL_ opponent comes back. Otherwise we hope in
	 * opponent's tenuki too much and play out ladders. :-) */
	if (!is_pass(mc->last_hint)
	    && unlikely(!coord_eq(b->last_move.coord, mc->last_hint))
	    && b->last_move.color == our_real_color
	    && fast_random(100) < mc->last_hint_value) {
		coord_t urgent = mc->last_hint;
		mc->last_hint = pass;
		return urgent;
	}

	/* In some of the cases, we pick atari response instead of random move.
	 * If there is an atari, capturing tends to be huge. */
	if (mc->capture_rate && fast_random(100) < mc->capture_rate) {
		mc->last_hint = domain_hint_capture(mc, b, b->last_move.coord);
		if (!is_pass(mc->last_hint)) {
			mc->last_hint_value = mc->capture_rate;
			return mc->last_hint;
		}
	}

	/* Maybe we can _put_ some stones into atari. That's cool. */
	if (mc->capture_rate && fast_random(100) < mc->atari_rate) {
		mc->last_hint = domain_hint_atari(mc, b, b->last_move.coord);
		if (!is_pass(mc->last_hint)) {
			mc->last_hint_value = mc->capture_rate;
			return mc->last_hint;
		}
	}

	/* Cutting is kinda urgent, too. */
	if (mc->cut_rate && fast_random(100) < mc->cut_rate) {
		mc->last_hint = domain_hint_cut(mc, b, b->last_move.coord);
		if (!is_pass(mc->last_hint)) {
			mc->last_hint_value = mc->cut_rate;
			return mc->last_hint;
		}
	}

	/* For the non-urgent moves, some of them will be contact play (tsuke
	 * or diagonal). These tend to be likely urgent. */
	if (mc->local_rate && fast_random(100) < mc->local_rate) {
		mc->last_hint = domain_hint_local(mc, b, b->last_move.coord);
		if (!is_pass(mc->last_hint)) {
			mc->last_hint_value = mc->local_rate;
			return mc->last_hint;
		}
	}

	return ((mc->last_hint = pass));
}
