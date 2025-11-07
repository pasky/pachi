//#define DEBUG
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

/* Full permit logic, ie m->coord may get changed to an alternative move */
static bool
playout_permit_move(playout_policy_t *p, board_t *b, move_t *m, bool alt, bool rnd)
{
	coord_t coord = m->coord;
	if (coord == pass) return false;

	bool permit =  (board_permit(b, m, NULL) &&
			(!p->permit || p->permit(p, b, m, alt, rnd)));

	if (DEBUGL(5)) {
		if (!permit)
			fprintf(stderr, "Playout permit(%s): rejected\n", coord2sstr(coord));
		if (permit && m->coord != coord)
			fprintf(stderr, "Playout permit(%s): redirect -> %s\n", coord2sstr(coord), coord2sstr(m->coord));
	}

	return permit;
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

	//if (DEBUGL(5)) fprintf(stderr, "Trying %s\n", coord2sstr(m->coord));
	return playout_permit_move(policy, b, m, true, true);
}


coord_t
playout_play_move(playout_setup_t *setup,
		  board_t *b, enum stone color,
		  playout_policy_t *policy)
{
	coord_t coord = pass;
	
	coord = policy->choose(policy, setup, b, color);
	if (DEBUGL(5))  fprintf(stderr, "Playout move: %s\n", coord2sstr(coord));
	coord = playout_check_move(policy, b, coord, color);

	if (!is_pass(coord)) {
		move_t m = move(coord, color);
		int r = board_play(b, &m);
		if (unlikely(r < 0)) {
			board_print(b, stderr);
			die("Picked playout move %s %s is ILLEGAL:\n", stone2str(color), coord2sstr(coord));
		}
		return coord;
	}

	/* Defer to uniformly random move choice if policy failed to produce a move.
	 * This must never happen if the policy is tracking internal board state, obviously. */
	if (DEBUGL(5))  fprintf(stderr, "Playout random move:\n");
	assert(!policy->setboard || policy->setboard_randomok);
	board_play_random(b, color, &coord, random_permit_handler, policy);
	if (DEBUGL(5))  fprintf(stderr, "Playout random move: %s\n", coord2sstr(coord));
	return coord;
}

static bool
check_bent_four_surrounding(board_t *b, enum stone other_color, coord_t lib, group_t wanted_surrounding)
{
	group_t surrounding = 0;
	foreach_neighbor(b, lib, {
		if (board_at(b, c) == other_color) {
			surrounding = group_at(b, c);
			if (surrounding != wanted_surrounding)
				return false;
		}
	});
	return (surrounding != 0);
}

/* Fill bent-four in the corner:
 * 
 *   | . . . . . .       | O O O . . .              | X X O O . .     | . . . . . .
 *   | O O O O O .   or  | X X O . . .	   but not  | . X X O . .     | O O . . . .
 *   | X X X X O .       | * X O O O .	            | O . X O . .     | . O O O O O
 *   | * X . X O .       | O X X X O .              | O X X O . .     | O X X X . O
 *   | O O O X O .       | O O . X O .	            | O X O O . .     | O O . X O O
 *   +-------------      +------------	            +------------     +-------------
 *   
 *   color       : bent-four stones color         (white here, color to play)
 *   other_color : surrounding group color        (black here)
 *
 *   returns coord to fill to make bent-four (first found, pass if none).
 *   @bent4_lib:  bent-four last liberty
 *   @bent4_kill: killing move after capture  */
static coord_t
fill_bent_four(board_t *b, enum stone color, coord_t *bent4_lib, coord_t *bent4_kill)
{
	enum stone other_color = stone_other(color);
	int s = board_rsize(b);
	coord_t xs[] = { 1, 1, s, s },  dx[] = { 1,  1, -1, -1 };
	coord_t ys[] = { 1, s, 1, s },  dy[] = { 1, -1,  1, -1 };
	
	for (int i = 0; i < 4; i++) {
		coord_t corner = coord_xy(xs[i], ys[i]);
		group_t g = group_at(b, corner);
		if (!g || board_at(b, corner) != color ||
		    group_stone_count(b, g, 4) != 3 || board_group_info(b, g).libs != 2)
			continue;

		coord_t twotwo = coord_xy(xs[i] + dx[i], ys[i] + dy[i]);
		group_t surrounding = group_at(b, twotwo);
		if (!surrounding || board_at(b, twotwo) != other_color || board_group_info(b, surrounding).libs != 2)
			continue;

		/* check really surrounding */
		if (!check_bent_four_surrounding(b, other_color, board_group_info(b, g).lib[0], surrounding) ||
		    !check_bent_four_surrounding(b, other_color, board_group_info(b, g).lib[1], surrounding))
			continue;

		/* find suitable lib to fill  (first line and other coord == 2 or 3)  */
		coord_t fill;
		for (int j = 0; j < 2; j++) {
			fill = board_group_info(b, g).lib[j];
			int x = coord_x(fill),  y = coord_y(fill);
			
			if (x == xs[i] && (y == ys[i] + dy[i]  ||  y == ys[i] + 2 * dy[i])) {
				if (y == ys[i] + dy[i])   *bent4_kill = coord_xy(xs[i] + dx[i], ys[i]);   /* 3 in line horizontally */
				else			  *bent4_kill = coord_xy(xs[i], ys[i] + dy[i]);   /* bent-three */
				break;
			}
			
			if (y == ys[i] && (x == xs[i] + dx[i]  ||  x == xs[i] + 2 * dx[i])) {
				if (x == xs[i] + dx[i])   *bent4_kill = coord_xy(xs[i], ys[i] + dy[i]);   /* 3 in line vertically */
				else                      *bent4_kill = coord_xy(xs[i] + dx[i], ys[i]);   /* bent-three */
				break;
			}
			
			fill = pass;
		}
		if (fill == pass)  continue;
		
		move_t m = move(fill, color);
		if (board_permit(b, &m, NULL)) {
			*bent4_lib = board_group_other_lib(b, g, fill);
			return fill;
		}
	}

	return pass;
}

/* Fill bent-three in the corner:   (leads to bent-four)
 * 
 *   | O O O . . . 
 *   | X X O . . . 
 *   | . X O O O . 
 *   | O X X X O . 
 *   | * O . X O . 
 *   +-------------
 *   
 *   color       : bent-three stones color        (white here, color to play)
 *   other_color : surrounding group color        (black here)
 *
 *   returns coord to fill (first found, pass if none). */
static coord_t
fill_bent_three(board_t *b, enum stone color)
{
	enum stone other_color = stone_other(color);
	int s = board_rsize(b);
	coord_t xs[] = { 1, 1, s, s },  dx[] = { 1,  1, -1, -1 };
	coord_t ys[] = { 1, s, 1, s },  dy[] = { 1, -1,  1, -1 };
	
	for (int i = 0; i < 4; i++) {
		coord_t corner = coord_xy(xs[i], ys[i]);
		if (board_at(b, corner) != S_NONE)
			continue;
		
		coord_t c1 = coord_xy(xs[i], ys[i] + dy[i]);
		coord_t c2 = coord_xy(xs[i] + dx[i], ys[i]);
		if (board_at(b, c1) != color || board_at(b, c2) != color)
			continue;

		group_t g1 = group_at(b, c1);
		group_t g2 = group_at(b, c2);
		if (!group_is_onestone(b, g1) || !group_is_onestone(b, g2) ||
		    board_group_info(b, g1).libs != 2 || board_group_info(b, g2).libs != 2)
			continue;

		coord_t twotwo = coord_xy(xs[i] + dx[i], ys[i] + dy[i]);
		group_t surrounding = group_at(b, twotwo);
		if (!surrounding || board_at(b, twotwo) != other_color || board_group_info(b, surrounding).libs != 2)
			continue;

		/* Check really surrounding */
		coord_t libs[] = { coord_xy(xs[i], ys[i] + 2 * dy[i]),  coord_xy(xs[i] + 2 * dx[i], ys[i]) };
		assert(board_at(b, libs[0]) == S_NONE && board_at(b, libs[1]) == S_NONE);
		if (!check_bent_four_surrounding(b, other_color, libs[0], surrounding) ||
		    !check_bent_four_surrounding(b, other_color, libs[1], surrounding))
			continue;
		
		move_t m = move(corner, color);
		if (board_permit(b, &m, NULL))
			return corner;
	}

	return pass;
}

#define random_game_loop_stuff  \
		if (DEBUGL(5)) board_print(b, stderr); \
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
	b->playout_board = true;   // don't need board hash, history ...

	int starting_passes[S_MAX];
	memcpy(starting_passes, b->passes, sizeof(starting_passes));

	assert(setup && policy);
	int gamelen = setup->gamelen - b->moves;

	if (policy->setboard)
		policy->setboard(policy, b);

	enum stone color = starting_color;
	int passes = is_pass(last_move(b).coord) && b->moves > 0;

	/* Play until both sides pass, or we hit threshold. */
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord = playout_play_move(setup, b, color, policy);		
		random_game_loop_stuff
	}	

	int bent4_moves = -2;
	coord_t bent4_lib = pass;
	coord_t bent4_kill = pass;

	/* Play some more, handling bent-fours this time ...
	 * FIXME bent-four code really belongs in moggy but needs to be handled here.
	 *       Add some hooks and move this to moggy.c ... */
	passes = 0;
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord;
		
		/* Kill bent-four group after filling. */
		if (b->moves == bent4_moves + 2) {
			/* Kill group (or capture if opponent didn't take) */
			//fprintf(stderr, "bent-four: capture / kill ...\n");
			coord = (board_at(b, bent4_lib) == S_NONE ? bent4_lib : bent4_kill);
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r >= 0);
		}
		else    coord = playout_play_move(setup, b, color, policy);
		
		/* Fill bent-fours */
		if (coord == pass && (coord = fill_bent_four(b, color, &bent4_lib, &bent4_kill)) != pass) {
			//fprintf(stderr, "bent-four: filling ...\n");
			bent4_moves = b->moves;
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r >= 0);
		}

		/* Fill bent-threes */
		if (coord == pass && (coord = fill_bent_three(b, color)) != pass) {
			//fprintf(stderr, "bent-three: filling ...\n");
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r >= 0);
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

	return result;
}


void
playout_policy_done(playout_policy_t *p)
{
	if (p->done) p->done(p);
	if (p->data) free(p->data);
	free(p);
}


