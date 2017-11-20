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

/*     . O O O |                   We're white.
 *     . O X X |      . O O O |
 *     O O X . |      . O X X |    Are we about to break corner seki by playing at @coord ?
 *     O X X X |      O O X . |    Assumes selfatari checks already passed.
 *     O X O * |      O X X X |      - b has 2 libs and one eye
 *     O X O O |      O X * O |      - w has one eye in the corner (other lib)
 *     O X O . |      O X O . |      - w makes a dead shape selfatari by playing at @coord
 *    ---------+     ---------+        (selfatari checks passed, so we know shape is dead)   */
bool
breaking_corner_seki(struct board *b, coord_t coord, enum stone color)
{
	enum stone other_color = stone_other(color);	

	if (immediate_liberty_count(b, coord))  /* Other lib checked later ... */
		return false;
	
	/* Own group(s) with 2 libs nearby ? */
	group_t gi = 0;
	coord_t lib2 = 0;
	foreach_neighbor(b, coord, {
		group_t g = group_at(b, c);
		if (g && board_group_info(b, g).libs != 2) /* Check groups of both colors */
			return false;
		if (board_at(b, c) != color)  continue;

		if (!gi) {
			gi = g;
			lib2 = board_group_other_lib(b, gi, coord);
			continue;
		}		
		if (g == gi) continue;

		/* 2 groups ? Must share same libs */
		for (int i = 0; i < 2; i++) {
			coord_t lib = board_group_info(b, g).lib[i];
			if (lib != coord && lib != lib2)
				return false;
		}
	});
	if (!gi)  return false;

	/* Other lib is corner eye ? */
	int x = coord_x(lib2, b), y = coord_y(lib2, b);
	if ((x != 1 && x != real_board_size(b)) ||
	    (y != 1 && y != real_board_size(b)) ||
	    !board_is_one_point_eye(b, lib2, color))
		return false;	

	/* Find outside group */
	group_t out = 0;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != other_color)  continue;
		group_t g = group_at(b, c);
		if (!out)  {  out = g;  continue;  }
		/* Multiple outside groups ? */
		if (out != g)  return false;
	});
	if (!out)  return false;

	/* Outside group has one eye ? */
	coord_t out_other_lib = board_group_other_lib(b, out, coord);
	if (!board_is_one_point_eye(b, out_other_lib, other_color))
		return false;

	//fprintf(stderr, "corner seki break: %s %s\n", stone2str(color), coord2sstr(coord, b));
	//board_print(b, stderr);

	/* We don't capture anything so it's selfatari */
	return true;
}


/*   . O O O O |
 *   O . X X X |   We're black.
 *   O . X * O |
 *   O . X X O |   Are we about to break 3-stones seki by playing at @coord ?
 *   O . X . O |   Assumes selfatari checks passed, so b has some outside liberties.
 *   O . X X X |
 *   . O O O O |   */
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
	 *   . O O O . 	  . O O O .   */
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
