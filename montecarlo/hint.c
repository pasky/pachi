#include <stdio.h>
#include <stdlib.h>

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

static coord_t
domain_hint_atari(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* If we or our neighbors are in atari, fix that, (Capture or escape.)
	 * This test costs a lot of performance (the whole playout is about 1/4
	 * slower), but improves the playouts a lot. */

	if (MCDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-urgent moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t urgents[5]; int urgents_len = 0;
	memset(urgents, 0, sizeof(urgents));

	coord_t fix;
	if (unlikely(board_group_in_atari(b, group_at(b, coord), &fix)))
		urgents[urgents_len++] = fix;
	foreach_neighbor(b, coord, {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_in_atari(b, group_at(b, c), &fix)))
			urgents[urgents_len++] = fix;
	} );

	if (unlikely(urgents_len)) {
		if (MCDEBUGL(8)) {
			fprintf(stderr, "Urgent moves found:");
			int i = 0;
			for (i = 0; i < urgents_len; i++)
				fprintf(stderr, " %d,%d", coord_x(urgents[i], b), coord_y(urgents[i], b));
			fprintf(stderr, "\n");
		}
		return urgents[fast_random(urgents_len)];
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

void
domain_hint(struct montecarlo *mc, struct board *b, coord_t *urgent, enum stone our_real_color)
{
	if (is_pass(b->last_move.coord))
		return;

	/* Now now, if we ignored an urgent move, the opponent will
	 * take it! */
	/* Note that we should use this only when the _REAL_ us tenukies
	 * and the _REAL_ opponent comes back. Otherwise we hope in
	 * opponent's tenuki too much and play out ladders. :-) */
	if (!is_pass(mc->last_hint)
	    && unlikely(!coord_eq(b->last_move.coord, mc->last_hint))
	    && b->last_move.color == our_real_color
	    && fast_random(100) < mc->last_hint_value) {
		*urgent = mc->last_hint;
		mc->last_hint = pass;
		return;
	}

	/* In some of the cases, we pick atari response instead of random move.
	 * If there is an atari, capturing tends to be huge. */
	if (mc->atari_rate && fast_random(100) < mc->atari_rate) {
		*urgent = domain_hint_atari(mc, b, b->last_move.coord);
		if (!is_pass(*urgent)) {
			mc->last_hint = *urgent;
			mc->last_hint_value = mc->atari_rate;
			return;
		}
	}

	/* Cutting is kinda urgent, too. */
	if (mc->cut_rate && fast_random(100) < mc->cut_rate) {
		*urgent = domain_hint_cut(mc, b, b->last_move.coord);
		if (!is_pass(*urgent)) {
			mc->last_hint = *urgent;
			mc->last_hint_value = mc->cut_rate;
			return;
		}
	}

	/* For the non-urgent moves, some of them will be contact play (tsuke
	 * or diagonal). These tend to be likely urgent. */
	if (mc->local_rate && fast_random(100) < mc->local_rate) {
		*urgent = domain_hint_local(mc, b, b->last_move.coord);
		if (!is_pass(*urgent)) {
			mc->last_hint = *urgent;
			mc->last_hint_value = mc->local_rate;
			return;
		}
	}

	mc->last_hint = pass;
}
