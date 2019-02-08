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
playout_permit_move(playout_policy_t *p, board_t *b, move_t *m, bool alt, bool rnd)
{
	coord_t coord = m->coord;
	if (coord == pass) return false;

	if (!board_permit(b, m, NULL) ||
	    (p->permit && !p->permit(p, b, m, alt, rnd)))
		return false;
	return true;
}

/* Return coord if move is ok, an alternative move or pass if not.
 * Not to be used with randomly picked moves (calls permit_move() with rnd=false). */
static coord_t
playout_check_move(playout_policy_t *p, board_t *b, coord_t coord, enum stone color)
{
	move_t m = move(coord, color);
	if (!playout_permit_move(p, b, &m, true, false))
		return pass;
	return m.coord;
}

/* Is *this* move permitted ? 
 * Called by policy permit() to check something so never the main permit() call. */
bool
playout_permit(playout_policy_t *p, board_t *b, coord_t coord, enum stone color, bool rnd)
{
	move_t m = move(coord, color);
	return playout_permit_move(p, b, &m, false, rnd);
}

static bool
random_permit_handler(board_t *b, move_t *m, void *data)
{
	playout_policy_t *policy = (playout_policy_t*)data;
	return playout_permit_move(policy, b, m, true, true);
}


coord_t
playout_play_move(playout_setup_t *setup,
		  board_t *b, enum stone color,
		  playout_policy_t *policy)
{
	coord_t coord = pass;
	
	if (is_pass(coord)) {
		coord = policy->choose(policy, setup, b, color);
		coord = playout_check_move(policy, b, coord, color);
		// fprintf(stderr, "policy: %s\n", coord2sstr(coord));
	}

	if (is_pass(coord)) {
	play_random:
		/* Defer to uniformly random move choice. */
		/* This must never happen if the policy is tracking
		 * internal board state, obviously. */
		assert(!policy->setboard || policy->setboard_randomok);
		board_play_random(b, color, &coord, random_permit_handler, policy);

	} else {
		move_t m = move(coord, color);
		if (board_play(b, &m) < 0) {
			if (PLDEBUGL(4)) {
				fprintf(stderr, "Pre-picked move %d,%d is ILLEGAL:\n", coord_x(coord), coord_y(coord));
				board_print(b, stderr);
			}
			goto play_random;
		}
	}

	return coord;
}

/*   | . . . . . .
 *   | O O O O O . 
 *   | X X X X O . 
 *   | * X . X O . 
 *   | O O O X O . 
 *   +-------------
 */

static coord_t
fill_bent_four(board_t *b, enum stone color, coord_t *other, coord_t *kill)
{
	enum stone other_color = stone_other(color);  // white here
	int s = board_rsize(b);
	coord_t corners[4] = { coord_xy(1, 1),
			       coord_xy(1, s),
			       coord_xy(s, 1),
			       coord_xy(s, s),
	};
	
	for (int i = 0; i < 4; i++) {
		coord_t corner = corners[i];
		group_t g = group_at(b, corner);
		if (!g || board_at(b, corner) != other_color ||
		    immediate_liberty_count(b, corner) != 1 ||
		    group_stone_count(b, g, 4) != 3 || board_group_info(b, g).libs != 2)
			continue;

		
		coord_t stone3 = pass;
		int x = coord_x(corner);
		int y = coord_y(corner);

		/* check 3 in line, horizontal */
		int dx = (x == 1 ? 1 : -1);
		for (int j = 0; j < 3; j++) {
			coord_t c = coord_xy(x + j * dx, y);
			if (board_at(b, c) != other_color)  break;
			if (j == 2)  {  stone3 = c;  *kill = coord_xy(x + dx, y);  }
		}

		/* check 3 in line, vertical */
		int dy = (y == 1 ? 1 : -1);
		for (int j = 0; j < 3; j++) {
			coord_t c = coord_xy(x, y + j * dy);
			if (board_at(b, c) != other_color)  break;
			if (j == 2)  {  stone3 = c;  *kill = coord_xy(x, y + dy);  }
		}

		if (stone3 == pass)  continue;

		group_t surrounding = 0;
		foreach_neighbor(b, stone3, {
				if (board_at(b, c) == color) {  surrounding = group_at(b, c);  break;  }
			});
		if (!surrounding || board_group_info(b, surrounding).libs != 2)  continue;

		coord_t fill = pass;
		foreach_neighbor(b, corner, {
				if (board_at(b, c) == S_NONE)  {  fill = c;  break;  }
			});

		move_t m = move(fill, other_color);
		if (board_permit(b, &m, NULL)) {
			*other = board_group_other_lib(b, g, fill);
			return fill;
		}
	}

	return pass;
}

#define random_game_loop_stuff  \
		if (PLDEBUGL(7)) { \
			fprintf(stderr, "%s %s\n", stone2str(color), coord2sstr(coord)); \
			if (PLDEBUGL(8)) board_print(b, stderr); \
		} \
\
		if (unlikely(is_pass(coord)))  passes++; \
		else                           passes = 0; \
\
		if (amafmap) { \
			assert(amafmap->gamelen < MAX_GAMELEN); \
			amafmap->is_ko_capture[amafmap->gamelen] = board_playing_ko_threat(b); \
			amafmap->game[amafmap->gamelen++] = coord; \
		} \
\
		if (setup->mercymin && abs(b->captures[S_BLACK] - b->captures[S_WHITE]) > setup->mercymin) \
			break; \
\
		color = stone_other(color);



int
playout_play_game(playout_setup_t *setup,
		  board_t *b, enum stone starting_color,
		  playout_amafmap_t *amafmap,
		  ownermap_t *ownermap,
		  playout_policy_t *policy)
{
	b->playout_board = true;   // don't need board hash, history, symmetry...

	int starting_passes[S_MAX];
	memcpy(starting_passes, b->passes, sizeof(starting_passes));

	assert(setup && policy);
	int gamelen = setup->gamelen - b->moves;

	if (policy->setboard)
		policy->setboard(policy, b);
#ifdef DEBUGL_BY_PLAYOUT
	int debug_level_orig = debug_level;
	debug_level = policy->debug_level;
#endif

	enum stone color = starting_color;
	int passes = is_pass(last_move(b).coord) && b->moves > 0;

	/* Play until both sides pass, or we hit threshold. */
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord = playout_play_move(setup, b, color, policy);		
		random_game_loop_stuff
	}	

	int bent4_moves = -2;
	coord_t bent4_other = pass;
	coord_t bent4_kill = pass;

	/* Play some more, handling bent-fours this time ...
	 * FIXME bent-four code really belongs in moggy but needs to be handled here.
	 *       Add some hooks and move this to moggy.c ... */
	passes = 0;
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord;
		
		/* Kill bent-four group after filling. */
		if (b->moves == bent4_moves + 1) {
			/* Capture or kill group. */
			coord = (board_at(b, bent4_other) == S_NONE ? bent4_other : bent4_kill);
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r == 0);
		}
		else    coord = playout_play_move(setup, b, color, policy);
		
		/* Fill bent-fours */
		if (coord == pass && (coord = fill_bent_four(b, stone_other(color), &bent4_other, &bent4_kill)) != pass) {
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r == 0);
			bent4_moves = b->moves;
		}

		random_game_loop_stuff
	}
	
	/* Territory scoring: score starting board, using playouts as confirmation phase.
	 * Like in a real game where players disagree about life and death:
	 * They play it out and rewind state for scoring once agreement is reached.
	 * Trying to score final boards directly is too noisy, random passes change the score...
	 * TODO: handle eyes in seki according to japanese rules. */
	if (b->rules == RULES_JAPANESE) {
		memcpy(b->passes, starting_passes, sizeof(starting_passes));
		last_move(b).color = stone_other(starting_color);
	}
	
	floating_t score = board_fast_score(b);
	int result = (starting_color == S_WHITE ? score * 2 : - (score * 2));

	if (DEBUGL(6)) {
		fprintf(stderr, "Random playout result: %d (W %f)\n", result, score);
		if (DEBUGL(7))  board_print(b, stderr);
	}

	if (ownermap)  ownermap_fill(ownermap, b);

#ifdef DEBUGL_BY_PLAYOUT
	debug_level = debug_level_orig;
#endif

	return result;
}


void
playout_policy_done(playout_policy_t *p)
{
	if (p->done) p->done(p);
	if (p->data) free(p->data);
	free(p);
}


