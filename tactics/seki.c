#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

#define DEBUG
#include "board.h"
#include "debug.h"
#include "tactics/1lib.h"
#include "tactics/dragon.h"
#include "tactics/seki.h"

/*  X X X   
    X . O   (We're black here)
    X X O   Are we about to break a 3-stones seki by playing at @coord ?
    X . O   Assumes selfatari checks passed, so b has outside liberties.
    X X X   
*/
bool
breaking_3_stone_seki(struct board *b, coord_t coord, enum stone color)
{
	enum stone other_color = stone_other(color);
	
	/* Opponent's 3-stone group with 2 libs nearby ? */
	group_t g3 = 0;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != other_color)
			continue;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs != 2 ||
		    group_stone_count(b, g, 4) != 3)
			return false;
		if (g3)  /* Multiple groups or bad bent-3 */
			return false;
		g3 = g;
	});
	if (!g3)
		return false;

	/* Check neighbours of the 2 liberties first (also checks shape :) */
	// FIXME is this enough to check all the bad shapes ?
	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, g3).lib[i];
		if (immediate_liberty_count(b, lib) >= 1)
			return false;  /* Bad shape or can escape */
		if (neighbor_count_at(b, lib, other_color) >= 2)
			return false;  /* Bad bent-3 or can connect out */
	}

	/*  Anything with liberty next to bent-3's center is no seki:
	 *   . O O O .    . O O O .   
	 *   O O X O O 	  O O X . O 
	 *   O X X . O 	  O X X O O 
	 *   O O . O O 	  O O . O O 
	 *   . O O O . 	  . O O O . 
	 */
	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, g3).lib[i];
		foreach_neighbor(b, lib, {   /* Find adjacent stone */
			if (board_at(b, c) == other_color &&
			    neighbor_count_at(b, c, other_color) == 2)
				return false;
			break;        /* Bad bent-3 already taken care of */
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

	/* Check group is completely surrounded.
	 * If it can countercapture for sure it's not completely surrounded */
	if (can_countercapture(b, g3, NULL, 0))
		return false;
	int visited[BOARD_MAX_COORDS] = {0, };
	if (!big_eye_area(b, color, group_base(g3), visited))
		return false;
	
	/* Already have 2 eyes ? No need for seki then */
	int eyes = 1;
	if (dragon_is_safe_full(b, own, color, visited, &eyes))
		return false;

	/* Safe after capturing these stones ? */
	bool safe = false;
	coord_t lib1 = board_group_info(b, g3).lib[0];
	coord_t lib2 = board_group_info(b, g3).lib[1];
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
