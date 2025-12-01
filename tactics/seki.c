#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

//#define DEBUG
#include "board.h"
#include "board_undo.h"
#include "debug.h"
#include "tactics/1lib.h"
#include "tactics/dragon.h"
#include "tactics/selfatari.h"
#include "tactics/seki.h"


/* Breaking local seki at c:
 *   - 2 opposing groups with 2 libs
 *   - c             selfatari for both
 *   - our other lib selfatari for both
 *   - opp other lib selfatari for both (if different)
 *
 * This way it works for all kinds of sekis whether the liberties are shared
 * or not, adjacent or not, in eyes etc (see t-unit/moggy_seki.t for example).
 * Symmetric so no need to pass color */
bool
breaking_local_seki(board_t *b, selfatari_state_t *s, group_t c)
{
	assert(board_at(b, c) == S_NONE);
	if (!s->groupcts[S_BLACK] || !s->groupcts[S_WHITE])
		return false;

	/* 2 opposing groups with 2 libs */
	group_t g  = s->groupids[S_BLACK][0];        assert(g  && board_at(b, g)  == S_BLACK);
	group_t g2 = s->groupids[S_WHITE][0];        assert(g2 && board_at(b, g2) == S_WHITE);
	if (group_libs(b, g)  != 2 ||
	    group_libs(b, g2) != 2)
		return false;
	
	/* Play at c selfatari for both */
	if (!(is_selfatari(b, S_BLACK, c) &&
	      is_selfatari(b, S_WHITE, c)))
		return false;

	/* Play at our other lib also */
	coord_t other  = group_other_lib(b, g, c);
	if (!(is_selfatari(b, S_BLACK, other) &&
	      is_selfatari(b, S_WHITE, other)))
		return false;

	/* Play at opp other lib also, if different */
	coord_t other2 = group_other_lib(b, g2, c);
	if (other2 != other &&
	    !(is_selfatari(b, S_BLACK, other2) &&
	      is_selfatari(b, S_WHITE, other2)))
		return false;

	return true;
}


/*   . . O O O |   We're black.
 *   O O O X X |   
 *   O X X X * |   Are we about to break false eye seki ?
 *   O X O O X |     - b about to fill false eye
 *   O X . O . |     - b groups 2 libs
 *  -----------+     - dead shape after filling eye    
 *
 *  breaking_local_seki() doesn't handle this, not selfatari... */
bool
breaking_false_eye_seki(board_t *b, coord_t coord, enum stone color)
{
	enum stone other_color = stone_other(color);	
	if (!board_is_eyelike(b, coord, color))
		return false;

	/* Find 2 own groups with 2 libs nearby */
	group_t g1 = 0, g2 = 0;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != color)  continue;  /* And can't be other color since eyelike */
		group_t g = group_at(b, c);
		if (group_libs(b, g) != 2)  return false;

		if (!g1)     {  g1 = g;  continue;  }
		if (g == g1)             continue;
		if (!g2)     {  g2 = g;  continue;  }
		if (g == g2)             continue;
		return false;  /* 3+ groups */
	});
	if (!g1 || !g2)  return false;
	
	/* Find inside group */
	coord_t lib2 = group_other_lib(b, g1, coord);
	group_t in = 0;
	foreach_neighbor(b, lib2, {
		if (board_at(b, c) != other_color)  continue;
		group_t g = group_at(b, c);
		if (group_libs(b, g) != 2)  return false;
		if (!in)  {  in = g;  continue;  }		
		if (in != g)  return false;  /* Multiple inside groups */
	});
	if (!in)  return false;

	coord_t lib3 = group_other_lib(b, g2, coord);
	if (group_other_lib(b, in, lib2) != lib3)
		return false;
	
	return true;
}


/*   . O O O O |       . O O O O |
 *   O . X X X |       O . X X X |      We're black.
 *   O . X * O |       O . X O * |
 *   O . X X O |       O . X O X |      Are we about to break 3-stones seki by playing at @coord ?
 *   O . X . O |       O . X O . |      Assumes selfatari checks passed, so we have some outside liberties.
 *   O . X X X |       O . X X X |
 *   . O O O O |       . O O O O |      */
bool
breaking_3_stone_seki(board_t *b, coord_t coord, enum stone color)
{
	enum stone other_color = stone_other(color);
	
	/* Opponent's 3-stone group with 2 libs nearby ? */
	group_t g3 = 0;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != other_color)
			continue;
		group_t g = group_at(b, c);
		if (group_libs(b, g) != 2 ||
		    group_stone_count(b, g, 4) != 3)
			return false;
		if (g3)  /* Multiple groups or bad bent-3 */
			return false;
		g3 = g;
	});
	if (!g3)
		return false;

	/* Check neighbours of the 2 liberties first (also checks shape :) */
	// XXX is this enough to check all the bad shapes ?
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g3, i);
		if (immediate_liberty_count(b, lib) >= 1)
			return false;  /* Bad shape or can escape */
		if (neighbor_count_at(b, lib, other_color) >= 2)
			return false;  /* Dead bent-3 or can connect out */
	}

	/*  Anything with liberty next to 3 stones' center is no seki:
	 *   . O O O .    . O O O .    . O O O .
	 *   O O X O O 	  O O X . O    O O . O O
	 *   O X X . O 	  O X X O O    O X X X O
	 *   O O . O O 	  O O . O O    O . O O O
	 *   . O O O . 	  . O O O .    O O O . .   */
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g3, i);
		foreach_neighbor(b, lib, {   /* Find adjacent stone */
			if (board_at(b, c) == other_color &&
			    neighbor_count_at(b, c, other_color) != 1)
				return false;
			break;        /* Dead bent-3 already taken care of */
		});
	}

	/* Find our group */
	group_t own = 0;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != color)
			continue;
		// FIXME multiple own groups around ?
		own = group_at(b, c);
	});
	if (!own)
		return false;

	/* Check 3-stone group is completely surrounded.
	 * Can't escape and can't connect out, only countercaptures left to check. */
	if (can_countercapture(b, g3, NULL))
		return false;

	/* Group alive after capturing these stones ? */
	bool safe = false;
	coord_t lib1 = group_lib(b, g3, 0);
	coord_t lib2 = group_lib(b, g3, 1);
	with_move(b, lib1, color, {
		with_move(b, lib2, color, {
			group_t g = group_at(b, own);
			assert(g);  assert(!group_at(b, g3));
			safe = dragon_is_safe(b, g, color);
		});
	});
	if (safe)
		return false;

	return true;
}
