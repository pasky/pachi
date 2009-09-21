#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "board.h"
#include "playout/old.h"
#include "playout.h"
#include "random.h"


/* The following arguments tune domain-specific heuristics.
 * capture_rate=MC_CAPTURERATE	how many of 100 moves should be non-random but
 * 				fix local atari, if there is any
 * atari_rate=MC_ATARIRATE	how many of 100 moves should be non-random but
 * 				make an atari, if there is any
 * local_rate=MC_LOCALRATE	how many of 100 moves should be contact plays
 * 				(tsuke or diagonal)
 * cut_rate=MC_CUTRATE		how many of 100 moves should fix local cuts,
 * 				if there are any */

#define MC_CAPTURERATE	50
#define MC_ATARIRATE	50
#define MC_CUTRATE	40
#define MC_LOCALRATE	30


/* *** Domain-specific knowledge comes here (that is, any heuristics that perfer
 * certain moves, aside of requiring the moves to be according to the rules). */
/* NOTE: This heuristics affects ONLY the random playouts! It does not help the
 * engine directly to pick a move, but it makes it pick the hinted moves in the
 * random playouts FROM the random initial move. So the engine will not prefer
 * to fix atari on the current board, but it will fix it as the other player
 * when the next move on current board failed to deal with it. */


struct old_policy {
	int capture_rate, atari_rate, cut_rate, local_rate;

	coord_t last_hint;
	int last_hint_value;
};

#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)


static coord_t
domain_hint_capture(struct playout_policy *p, struct board *b, coord_t coord)
{
	/* If we or our neighbors are in atari, fix that, (Capture or escape.)
	 * This test costs a lot of performance (the whole playout is about 1/4
	 * slower), but improves the playouts a lot. */

	if (PLDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-capture moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t captures[5]; int captures_len = 0, capture_choice = 0;
	memset(captures, 0, sizeof(captures));

	coord_t fix;
	if (unlikely(board_group_in_atari(b, group_at(b, coord), &fix)) && likely(is_selfatari(b, board_at(b, coord), fix))) {
		/* We can capture the opponent! Don't even think about escaping
		 * our own ataris then. */
		captures[captures_len] = fix;
		capture_choice = captures_len++;
		goto choosen;
	}
	foreach_neighbor(b, coord, {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_in_atari(b, group_at(b, c), &fix)) && likely(is_selfatari(b, board_at(b, c), fix)))
			captures[captures_len++] = fix;
	} );

	if (unlikely(captures_len)) {
		capture_choice = fast_random(captures_len);
choosen:
		if (PLDEBUGL(8)) {
			fprintf(stderr, "capture moves found:");
			int i = 0;
			for (i = 0; i < captures_len; i++)
				fprintf(stderr, " %c%d,%d",
					capture_choice == i ? '*' : ' ',
					coord_x(captures[i], b), coord_y(captures[i], b));
			fprintf(stderr, "\n");
		}
		return captures[capture_choice];
	}
	return pass;
}


static bool inline
valid_atari_move(struct board *b, coord_t coord)
{
	/* Do not avoid atari moves that the opponent actuall can never
	 * play because they are one of our eyes. Without this, the
	 * atari avoidance will actually fill one eye of surrounded
	 * two-eyed group. */
	return board_get_one_point_eye(b, &coord) == S_NONE;
}

static bool inline
validate_atari_pair(struct board *b, coord_t coord[2])
{
	/* DTRT if only one of the atari moves is sensible. */
	bool v[2] = { valid_atari_move(b, coord[0]), valid_atari_move(b, coord[1]) };
	if (likely(v[0] && v[1]))
		return true;
	if (v[0]) {
		coord[1] = coord[0];
		return true;
	}
	if (v[1]) {
		coord[0] = coord[1];
		return true;
	}
	return false;
}

static coord_t
domain_hint_atari(struct playout_policy *p, struct board *b, coord_t coord)
{
	/* If we can put the last-move group in atari, do that. */

	if (PLDEBUGL(8)) {
		fprintf(stderr, "-- Scanning for %d,%d-atari moves:\n", coord_x(coord, b), coord_y(coord, b));
		board_print(b, stderr);
	}

	coord_t ataris[5][2]; int ataris_len = 0, atari_choice = 0;
	memset(ataris, 0, sizeof(ataris));

	if (unlikely(board_group_can_atari(b, group_at(b, coord), ataris[ataris_len]))) {
		if (likely(validate_atari_pair(b, ataris[ataris_len]))) {
			/* Atari-ing opponent is always better than preventing
			 * opponent atari-ing us. */
			atari_choice = ataris_len++;
			goto choosen;
		}
	}
	foreach_neighbor(b, coord, {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_can_atari(b, group_at(b, c), ataris[ataris_len])))
			if (likely(validate_atari_pair(b, ataris[ataris_len])))
				ataris_len++;
	} );

	if (unlikely(ataris_len)) {
		atari_choice = fast_random(ataris_len);
choosen:
		if (PLDEBUGL(8)) {
			fprintf(stderr, "atari moves found:");
			int i = 0;
			for (i = 0; i < ataris_len; i++)
				fprintf(stderr, " %c%d,%d;%d,%d",
					atari_choice == i ? '*' : ' ',
					coord_x(ataris[i][0], b), coord_y(ataris[i][0], b),
					coord_x(ataris[i][1], b), coord_y(ataris[i][1], b));
			fprintf(stderr, "\n");
		}
		return ataris[atari_choice][fast_random(2)];
	}
	return pass;
}

static coord_t
domain_hint_cut(struct playout_policy *p, struct board *b, coord_t coord)
{
	/* Check if this move is cutting kosumi:
	 * (O) X
	 *  X  .  */

	if (PLDEBUGL(8)) {
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
				coord_raw(cutted) -= board_size(b);
			else
				coord_raw(cutted) += board_size(b);
			if (likely(board_at(b, cutted) != cutting_color))
				continue;
			/* Cut kosumi! */
			cuts[cuts_len++] = c;
		}
	} foreach_diag_neighbor_end;

	if (unlikely(cuts_len)) {
		if (PLDEBUGL(8)) {
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
domain_hint_local(struct playout_policy *p, struct board *b, coord_t coord)
{
	/* Pick a suitable move that is directly or diagonally adjecent. In the
	 * real game, local moves often tend to be the urgent ones, even if they
	 * aren't atari. */

	if (PLDEBUGL(8)) {
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
		if (PLDEBUGL(8)) {
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
playout_old_choose(struct playout_policy *p, struct board *b, enum stone our_real_color)
{
	struct old_policy *pp = p->data;

	if (is_pass(b->last_move.coord))
		return pass;

	/* Now now, if we ignored an urgent move, the opponent will
	 * take it! */
	/* Note that we should use this only when the _REAL_ us tenukies
	 * and the _REAL_ opponent comes back. Otherwise we hope in
	 * opponent's tenuki too much and play out ladders. :-) */
	if (!is_pass(pp->last_hint)
	    && unlikely(!coord_eq(b->last_move.coord, pp->last_hint))
	    && b->last_move.color == our_real_color
	    && fast_random(100) < pp->last_hint_value) {
		coord_t urgent = pp->last_hint;
		pp->last_hint = pass;
		return urgent;
	}

	/* In some of the cases, we pick atari response instead of random move.
	 * If there is an atari, capturing tends to be huge. */
	if (pp->capture_rate && fast_random(100) < pp->capture_rate) {
		pp->last_hint = domain_hint_capture(p, b, b->last_move.coord);
		if (!is_pass(pp->last_hint)) {
			pp->last_hint_value = pp->capture_rate;
			return pp->last_hint;
		}
	}

	/* Maybe we can _put_ some stones into atari. That's cool. */
	if (pp->capture_rate && fast_random(100) < pp->atari_rate) {
		pp->last_hint = domain_hint_atari(p, b, b->last_move.coord);
		if (!is_pass(pp->last_hint)) {
			pp->last_hint_value = pp->capture_rate;
			return pp->last_hint;
		}
	}

	/* Cutting is kinda urgent, too. */
	if (pp->cut_rate && fast_random(100) < pp->cut_rate) {
		pp->last_hint = domain_hint_cut(p, b, b->last_move.coord);
		if (!is_pass(pp->last_hint)) {
			pp->last_hint_value = pp->cut_rate;
			return pp->last_hint;
		}
	}

	/* For the non-urgent moves, some of them will be contact play (tsuke
	 * or diagonal). These tend to be likely urgent. */
	if (pp->local_rate && fast_random(100) < pp->local_rate) {
		pp->last_hint = domain_hint_local(p, b, b->last_move.coord);
		if (!is_pass(pp->last_hint)) {
			pp->last_hint_value = pp->local_rate;
			return pp->last_hint;
		}
	}

	return ((pp->last_hint = pass));
}


struct playout_policy *
playout_old_init(char *arg)
{
	struct playout_policy *p = calloc(1, sizeof(*p));
	struct old_policy *pp = calloc(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_old_choose;

	pp->last_hint = pass;

	pp->capture_rate = MC_CAPTURERATE;
	pp->atari_rate = MC_ATARIRATE;
	pp->local_rate = MC_LOCALRATE;
	pp->cut_rate = MC_CUTRATE;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capture_rate = atoi(optval);
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				pp->atari_rate = atoi(optval);
			} else if (!strcasecmp(optname, "localrate") && optval) {
				pp->local_rate = atoi(optval);
			} else if (!strcasecmp(optname, "cutrate") && optval) {
				pp->cut_rate = atoi(optval);
			} else {
				fprintf(stderr, "playout-old: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	return p;
}
