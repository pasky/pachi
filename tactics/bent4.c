#define DEBUG
#include <assert.h>
#include <stdio.h>

#include "board.h"
#include "debug.h"
#include "tactics/bent4.h"


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


/* Must be called when boardsize changes. */
void
bent4_statics_init(int boardsize)
{
	bent4_statics_t *bs = &bent4_statics;
	int s = boardsize;

	/* Run once per boardsize */
	if (bs->boardsize == s)
		return;
	bs->boardsize = s;

	/* (cx, cy): corner coords   (dx, dy): direction towards center */
	coord_t cx_[] = { 1, 1, s, s },  dx_[] = { 1,  1, -1, -1 };
	coord_t cy_[] = { 1, s, 1, s },  dy_[] = { 1, -1,  1, -1 };

	for (int i = 0; i < 4; i++) {
		int cx = cx_[i], cy = cy_[i], dx = dx_[i], dy = dy_[i];
		bs->p11[i] = coord_xy(cx          ,  cy          );
		bs->p22[i] = coord_xy(cx + dx     ,  cy + dy     );
		bs->p12[i] = coord_xy(cx          ,  cy + dy     );
		bs->p21[i] = coord_xy(cx + dx     ,  cy          );
		bs->p13[i] = coord_xy(cx          ,  cy + dy + dy);
		bs->p31[i] = coord_xy(cx + dx + dx,  cy          );
	}
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

/* Kill group, or capture if opponent didn't take. */
static coord_t
kill_bent4(bent4_t *b4, board_t *b, enum stone color)
{
	if (board_at(b, b4->kill) == S_NONE &&
	    immediate_liberty_count(b, b4->kill) > 1)
		return b4->kill;

	/* Must check for suicide here, opponent may not be in atari anymore. */
	if (board_at(b, b4->lib) == S_NONE &&
	    board_is_valid_play_no_suicide(b, color, b4->lib))
		return b4->lib;

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
 * and sets:
 *   b4->moves   (current move number)
 *   b4->lib     (bent-four last liberty)
 *   b4->kill    (killing move after capture)  */
static coord_t
fill_bent4(bent4_t *b4, board_t *b, enum stone color)
{
	enum stone other_color = stone_other(color);
	bent4_statics_t *bs = &bent4_statics;

	group_t g;
	coord_t lib;
	for (int i = 0; i < 4; i++) {
		coord_t corner = bs->p11[i];
		g = group_at(b, corner);
		if (!g || board_at(b, corner) != color ||
		    group_stone_count(b, g, 4) != 3 || group_libs(b, g) != 2)
			continue;

		coord_t twotwo = bs->p22[i];
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
			if (lib == bs->p12[i]) {  b4->kill = bs->p21[i];  goto found;  }

			/* bent-three */
			if (lib == bs->p13[i]) {  b4->kill = bs->p12[i];  goto found;  }

			/* 3 in line vertically */
			if (lib == bs->p21[i]) {  b4->kill = bs->p12[i];  goto found;  }

			/* bent-three */
			if (lib == bs->p31[i]) {  b4->kill = bs->p21[i];  goto found;  }
		}
	}
	return pass;

 found:
	assert(board_is_valid_play_no_suicide(b, color, lib));

	b4->moves = b->moves;
	b4->lib = group_other_lib(b, g, lib);
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
fill_bent3(board_t *b, enum stone color)
{
	bent4_statics_t *bs = &bent4_statics;
	enum stone other_color = stone_other(color);

	for (int i = 0; i < 4; i++) {
		coord_t corner = bs->p11[i];
		if (board_at(b, corner) != S_NONE)
			continue;

		coord_t c1 = bs->p12[i], c2 = bs->p21[i];
		if (board_at(b, c1) != color || board_at(b, c2) != color)
			continue;

		group_t g1 = group_at(b, c1), g2 = group_at(b, c2);
		if (!group_is_onestone(b, g1) || !group_is_onestone(b, g2) ||
		    group_libs(b, g1) != 2 || group_libs(b, g2) != 2)
			continue;

		coord_t twotwo = bs->p22[i];
		group_t surrounding = group_at(b, twotwo);
		if (!surrounding || board_at(b, twotwo) != other_color || group_libs(b, surrounding) != 2)
			continue;

		/* Check really surrounding */
		coord_t lib1 = bs->p13[i];
		coord_t lib2 = bs->p31[i];
		assert(board_at(b, lib1) == S_NONE && board_at(b, lib2) == S_NONE);
		if (!check_bent4_surrounding(b, other_color, lib1, surrounding) ||
		    !check_bent4_surrounding(b, other_color, lib2, surrounding))
			continue;

		assert(board_is_valid_play_no_suicide(b, color, corner));
		return corner;
	}

	return pass;
}

void
bent4_init(bent4_t *b4, board_t *b)
{
	/* Make sure bent4 statics are initialiazed. */
	assert(bent4_statics.boardsize == b->rsize);

	b4->moves = -2;
	b4->lib = pass;
	b4->kill = pass;
}

coord_t
bent4_play_move(bent4_t *b4, playout_t *playout, board_t *b, enum stone color)
{
	coord_t coord;

	/* Kill bent-four group after filling. */
	if (b->moves == b4->moves + 2 &&
	    (coord = kill_bent4(b4, b, color)) != pass) {
		if (DEBUGL(5))  fprintf(stderr, "Kill bent-four: %s\n", coord2sstr(coord));
		goto play_move;
	}

	/* Playout plays if it can. */
	if ((coord = playout_play_move(playout, b, color)) != pass)
		return coord;  /* Move already played */

	/* Fill bent-fours */
	if ((coord = fill_bent4(b4, b, color)) != pass) {
		if (DEBUGL(5))  fprintf(stderr, "Fill bent-four: %s\n", coord2sstr(coord));
		goto play_move;
	}

	/* Fill bent-threes */
	if ((coord = fill_bent3(b, color)) != pass) {
		if (DEBUGL(5))  fprintf(stderr, "Fill bent-three: %s\n", coord2sstr(coord));
		goto play_move;
	}

	/* Fallthrough = play pass. */

 play_move:;
	move_t m = move(coord, color);
	int r = board_play(b, &m);  assert(r >= 0);
	return coord;
}
