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

void
amaf_init(amafmap_t *amaf)
{
	amaf->gamelen = amaf->game_baselen = 0;
}

/* Full permit logic, ie m->coord may get changed to an alternative move */
static bool
playout_permit_move(playout_policy_t *p, board_t *b, move_t *m, bool alt, bool rnd)
{
	coord_t coord = m->coord;
	if (coord == pass) return false;

#ifdef EXTRA_CHECKS
	assert(is_player_color(m->color));
	assert(sane_coord(coord));
#endif

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
playout_play_move(playout_t *playout, board_t *b, enum stone color)
{
	playout_setup_t *setup = playout->setup;
	playout_policy_t *policy = playout->policy;
	coord_t coord, playout_coord;

	playout_coord = policy->choose(policy, setup, b, color);
	if (DEBUGL(5))  fprintf(stderr, "Playout move: %s\n", coord2sstr(playout_coord));
	coord = playout_check_move(policy, b, playout_coord, color);

	/* Show if playout move is rejected. */
	if (DEBUGL(5) && coord != playout_coord)
		fprintf(stderr, "Playout move %s was invalid !\n", coord2sstr(playout_coord));

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
	coord = board_play_random(b, color, random_permit_handler, policy);
	if (DEBUGL(5))  fprintf(stderr, "Playout random move: %s\n", coord2sstr(coord));
	return coord;
}

static bool
check_bent4_surrounding(board_t *b, enum stone other_color, coord_t lib, group_t wanted_surrounding)
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

/* Static bent-four data: cached coordinates for each corner. */
typedef struct {
	int boardsize;
	coord_t p11[4];   /*  For lower-left corner:          */
	coord_t p22[4];   /*                                  */
	coord_t p12[4];   /*    | B . .     1: (1,1) = p11    */
	coord_t p21[4];   /*    | A 2 .     2: (2,2) = p22    */
	coord_t p13[4];	  /*    | 1 A B     A: p12 and p21    */
	coord_t p31[4];   /*    +-------    B: p13 and p31    */
} bent4_statics_t;

static bent4_statics_t bent4_statics = { 0, };

void
bent4_statics_init(int boardsize)
{
	bent4_statics_t *bf = &bent4_statics;
	int s = boardsize;

	/* Run once per boardsize */
	if (bf->boardsize == s)
		return;
	bf->boardsize = s;

	/* (cx, cy): corner coords   (dx, dy): direction towards center */
	coord_t cx_[] = { 1, 1, s, s },  dx_[] = { 1,  1, -1, -1 };
	coord_t cy_[] = { 1, s, 1, s },  dy_[] = { 1, -1,  1, -1 };

	for (int i = 0; i < 4; i++) {
		int cx = cx_[i], cy = cy_[i], dx = dx_[i], dy = dy_[i];
		bf->p11[i] = coord_xy(cx          ,  cy          );
		bf->p22[i] = coord_xy(cx + dx     ,  cy + dy     );
		bf->p12[i] = coord_xy(cx          ,  cy + dy     );
		bf->p21[i] = coord_xy(cx + dx     ,  cy          );
		bf->p13[i] = coord_xy(cx          ,  cy + dy + dy);
		bf->p31[i] = coord_xy(cx + dx + dx,  cy          );
	}
}

/* Kill group, or capture if opponent didn't take. */
static coord_t
kill_bent4(board_t *b, enum stone color, coord_t bent4_lib, coord_t bent4_kill)
{
	if (board_at(b, bent4_kill) == S_NONE &&
	    immediate_liberty_count(b, bent4_kill) > 1)
		return bent4_kill;

	/* Must check for suicide here, opponent may not be in atari anymore. */
	if (board_at(b, bent4_lib) == S_NONE &&
	    board_is_valid_play_no_suicide(b, color, bent4_lib))
		return bent4_lib;

	return pass;
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
 * color       : bent-four stones color  (color to play)
 * other_color : surrounding group color
 *
 * returns coord to fill to make bent-four (first found, pass if none).
 * @bent4_lib:  bent-four last liberty
 * @bent4_kill: killing move after capture  */
static coord_t
fill_bent4(board_t *b, enum stone color, coord_t *bent4_lib, coord_t *bent4_kill)
{
	enum stone other_color = stone_other(color);
	bent4_statics_t *bf = &bent4_statics;

	group_t g;
	coord_t lib;
	for (int i = 0; i < 4; i++) {
		coord_t corner = bf->p11[i];
		g = group_at(b, corner);
		if (!g || board_at(b, corner) != color ||
		    group_stone_count(b, g, 4) != 3 || group_libs(b, g) != 2)
			continue;

		coord_t twotwo = bf->p22[i];
		group_t surrounding = group_at(b, twotwo);
		if (!surrounding || board_at(b, twotwo) != other_color || group_libs(b, surrounding) != 2)
			continue;

		/* check really surrounding */
		if (!check_bent4_surrounding(b, other_color, group_lib(b, g, 0), surrounding) ||
		    !check_bent4_surrounding(b, other_color, group_lib(b, g, 1), surrounding))
			continue;

		/* find suitable lib to fill  (first line and other coord == 2 or 3)  */
		for (int j = 0; j < 2; j++) {
			lib = group_lib(b, g, j);

			/* 3 in line horizontally */
			if (lib == bf->p12[i]) {  *bent4_kill = bf->p21[i];  goto found;  }

			/* bent-three */
			if (lib == bf->p13[i]) {  *bent4_kill = bf->p12[i];  goto found;  }

			/* 3 in line vertically */
			if (lib == bf->p21[i]) {  *bent4_kill = bf->p12[i];  goto found;  }

			/* bent-three */
			if (lib == bf->p31[i]) {  *bent4_kill = bf->p21[i];  goto found;  }
		}
	}
	return pass;

 found:
	assert(board_is_valid_play_no_suicide(b, color, lib));
	*bent4_lib = group_other_lib(b, g, lib);
	return lib;
}

/* Fill bent-three in the corner:   (leads to bent-four)
 *
 *   | O O O . . .
 *   | X X O . . .    color       : bent-three stones color (color to play)
 *   | . X O O O .    other_color : surrounding group color
 *   | O X X X O .
 *   | * O . X O .    returns coord to fill (first found, pass if none). 
 *   +-------------                                                      */
static coord_t
fill_bent_three(board_t *b, enum stone color)
{
	bent4_statics_t *bf = &bent4_statics;
	enum stone other_color = stone_other(color);

	for (int i = 0; i < 4; i++) {
		coord_t corner = bf->p11[i];
		if (board_at(b, corner) != S_NONE)
			continue;

		coord_t c1 = bf->p12[i], c2 = bf->p21[i];
		if (board_at(b, c1) != color || board_at(b, c2) != color)
			continue;

		group_t g1 = group_at(b, c1), g2 = group_at(b, c2);
		if (!group_is_onestone(b, g1) || !group_is_onestone(b, g2) ||
		    group_libs(b, g1) != 2 || group_libs(b, g2) != 2)
			continue;

		coord_t twotwo = bf->p22[i];
		group_t surrounding = group_at(b, twotwo);
		if (!surrounding || board_at(b, twotwo) != other_color || group_libs(b, surrounding) != 2)
			continue;

		/* Check really surrounding */
		coord_t lib1 = bf->p13[i];
		coord_t lib2 = bf->p31[i];
		assert(board_at(b, lib1) == S_NONE && board_at(b, lib2) == S_NONE);
		if (!check_bent4_surrounding(b, other_color, lib1, surrounding) ||
		    !check_bent4_surrounding(b, other_color, lib2, surrounding))
			continue;

		assert(board_is_valid_play_no_suicide(b, color, corner));
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


floating_t
playout_play_game(playout_t *playout, board_t *b, enum stone starting_color,
		  amafmap_t *amafmap, ownermap_t *ownermap)
{
	if (DEBUGL(5))  fprintf(stderr, "------------------------------- playout start -------------------------------\n\n");

	assert(playout && playout->setup && playout->policy);
	playout_setup_t *setup = playout->setup;
	playout_policy_t *policy = playout->policy;
	
	b->playout_board = true;   // don't need board hash, history ...

	int starting_passes[S_MAX];
	memcpy(starting_passes, b->passes, sizeof(starting_passes));

	int gamelen = setup->gamelen - b->moves;

	if (policy->setboard)
		policy->setboard(policy, b);

	enum stone color = starting_color;
	int passes = is_pass(last_move(b).coord) && b->moves > 0;

	/* Play until both sides pass, or we hit threshold. */
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord = playout_play_move(playout, b, color);
		random_game_loop_stuff
	}	

	if (DEBUGL(5))  fprintf(stderr, "------------------------------- bent4 handling -------------------------------\n\n");

	int bent4_moves = -2;
	coord_t bent4_lib = pass;
	coord_t bent4_kill = pass;
	assert(bent4_statics.boardsize == b->rsize);

	/* Play some more, handling bent-fours this time ...
	 * FIXME bent-four code really belongs in moggy but needs to be handled here.
	 *       Add some hooks and move this to moggy.c ... */
	passes = 0;
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord;
		
		/* Kill bent-four group after filling. */
		if (b->moves == bent4_moves + 2 &&
		    (coord = kill_bent4(b, color, bent4_lib, bent4_kill)) != pass) {
			if (DEBUGL(5))  fprintf(stderr, "Kill bent-four: %s\n", coord2sstr(coord));
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r >= 0);
		}
		else    coord = playout_play_move(playout, b, color);
		
		/* Fill bent-fours */
		if (coord == pass && (coord = fill_bent4(b, color, &bent4_lib, &bent4_kill)) != pass) {
			if (DEBUGL(5))  fprintf(stderr, "Fill bent-four: %s\n", coord2sstr(coord));
			bent4_moves = b->moves;
			move_t m = move(coord, color);
			int r = board_play(b, &m);  assert(r >= 0);
		}

		/* Fill bent-threes */
		if (coord == pass && (coord = fill_bent_three(b, color)) != pass) {
			if (DEBUGL(5))  fprintf(stderr, "Fill bent-three: %s\n", coord2sstr(coord));
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

	if (DEBUGL(5)) {
		fprintf(stderr, "Playout finished: score: %s+%.1f\n\n", (score > 0 ? "W" : "B"), fabs(score));
		fprintf(stderr, "------------------------------- playout end -------------------------------\n\n");
	}

	if (ownermap)  ownermap_fill(ownermap, b, score);

	return score;
}


void
playout_policy_done(playout_policy_t *p)
{
	if (p->done) p->done(p);
	if (p->data) free(p->data);
	free(p);
}


