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
#include "tactics/nakade.h"
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
#ifdef EXTRA_CHECKS
	assert(sane_coord(c));
	assert(board_at(b, c) == S_NONE);
#endif
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
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
	assert(board_at(b, coord) == S_NONE);
#endif
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

static bool
surrounding_groups_connected(board_t *b, group_t group)
{
	enum stone color = board_at(b, group);
	enum stone other_color = stone_other(color);
	mq_t groups;  mq_init(&groups);

	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != other_color) continue;
			group_t g = group_at(b, c);
			mq_add_nodup(&groups, g);
		});
	} foreach_in_group_end;
	assert(groups.moves);

	return same_dragon_groups(b, &groups);
}

/*   . O O O O |       . O O O O |
 *   O . X X X |       O . X X X |      We're black.
 *   O . X * O |       O . X O * |
 *   O . X X O |       O . X O X |      Are we about to break 3-stones seki by playing at @coord ?
 *   O . X . O |       O . X O . |      Assumes selfatari checks passed, so we have some outside liberties.
 *   O . X X X |       O . X X X |
 *   . O O O O |       . O O O O |      */
static bool
breaking_3_stone_seki(board_t *b, coord_t coord, enum stone color, group_t own, group_t g3)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
	assert(board_at(b, coord) == S_NONE);
	assert(sane_group(b, own));
	assert(sane_group(b, g3));
	assert(group_libs(b, g3) == 2);
	assert(group_stone_count(b, g3, 4) == 3);
#endif
	enum stone other_color = stone_other(color);
	
	/* Check neighbours of the 2 liberties first (also checks some shapes :) */
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g3, i);
		if (immediate_liberty_count(b, lib) >= 1)
			return false;  /* Bad shape or can escape */
		if (neighbor_count_at(b, lib, other_color) >= 2)
			return false;  /* Bent-3 + lib (can make dead shape) or can connect out */
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
			    neighbor_count_at(b, c, other_color) == 2)
				return false;
		});
	}

	// XXX is this enough to check all the bad shapes ?

	/* Check group is completely surrounded and surrounding groups are connected.
	 * Can't escape and can't connect out, only countercaptures left to check. */
	if (can_countercapture(b, g3, NULL) || !surrounding_groups_connected(b, g3))
		return false;

	/* Group alive after capturing these stones ? */
	bool safe = false;
	with_move_strict(b, coord, color, {
		assert(group_libs(b, g3) == 1);
		coord_t lib = group_lib(b, g3, 0);
		with_move_strict(b, lib, color, {
			group_t g = group_at(b, own);
			assert(g);  assert(!group_at(b, g3));
			safe = dragon_is_safe(b, g, color);
		});
	});
	return !safe;
}

/*   . . O O O |
 *   . O O X X |
 *   O . . X . |       We're black.
 *   O X X X O |
 *   O X * O O |       Are we about to break 4-stones seki by playing at @coord ?
 *   O X X X O |       Assumes selfatari checks passed, so we have some outside liberties.
 *   O . . X X |
 *   . O O O O |       */
static bool
breaking_4_stone_seki(board_t *b, coord_t coord, enum stone color, group_t own, group_t g4)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
	assert(board_at(b, coord) == S_NONE);
	assert(sane_group(b, own));
	assert(sane_group(b, g4));
	assert(group_libs(b, g4) == 2);
	assert(group_stone_count(b, g4, 5) == 4);
#endif
	enum stone other_color = stone_other(color);

	/* Check neighbours of the 2 liberties first (also checks some shapes :) */
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g4, i);
		if (immediate_liberty_count(b, lib) >= 1)
			return false;  /* Bad shape or can escape */
		if (neighbor_count_at(b, lib, other_color) >= 2)
			return false;  /* Bend + lib (can make dead shape), or can connect out */
	}

	/*  Anything with liberty next to 4 stones' center is no seki:
	 *   . O O O .    . O O O .    . O O O .
	 *   O O X O O 	  O O X . O    O O X O O
	 *   O X X . O 	  O X X . O    O X X . O
	 *   O O X O O 	  O O X O O    O . X O O
	 *   . O . O . 	  . O O O .    O O O . .   */
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g4, i);
		foreach_neighbor(b, lib, {   /* Find adjacent stone */
			if (board_at(b, c) == other_color &&
			    neighbor_count_at(b, c, other_color) == 3)
				return false;
		});
	}

	/* Check nakade shape is dead... */
	mq_t area;  mq_init(&area);
	for (coord_t c = g4; c; c = groupnext_at(b, c))
		mq_add(&area, c);
	assert(area.moves == 4);
	if (!nakade_area_dead_shape(b, &area))
		return false;
	
	/* But alive with extra move.
	 * (would have to check both moves without previous checks) */
	mq_add(&area, coord);
	if (nakade_area_dead_shape(b, &area))
		return false;

	/* Check group is completely surrounded and surrounding groups are connected.
	 * Can't escape and can't connect out, only countercaptures left to check. */
	if (can_countercapture(b, g4, NULL) || !surrounding_groups_connected(b, g4))
		return false;

	/* Group alive after capturing these stones ? */
	bool safe = false;
	with_move_strict(b, coord, color, {
		assert(group_libs(b, g4) == 1);
		coord_t lib = group_lib(b, g4, 0);
		with_move_strict(b, lib, color, {
			group_t g = group_at(b, own);
			assert(g);  assert(!group_at(b, g4));
			safe = dragon_is_safe(b, g, color);
		});
	});
	return !safe;
}

/*   . . O O O |
 *   . O O X X |
 *   O . X X . |       We're black.
 *   O X X O O |
 *   O X * O O |       Are we about to break 5-stones seki by playing at @coord ?
 *   O X X X O |       Assumes selfatari checks passed, so we have some outside liberties.
 *   O . . X X |
 *   . O O O O |       */
static bool
breaking_5_stone_seki(board_t *b, coord_t coord, enum stone color, group_t own, group_t g5)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
	assert(board_at(b, coord) == S_NONE);
	assert(sane_group(b, own));
	assert(sane_group(b, g5));
	assert(group_libs(b, g5) == 2);
	assert(group_stone_count(b, g5, 6) == 5);
#endif
	enum stone other_color = stone_other(color);

	/* Check neighbours of the 2 liberties first.
	 * Not as simple as 3-stones / 4-stones case:
	 * Adjacent liberties and liberty in empty triangle are fine here. */
	bool adjecent_libs = coord_is_adjecent(group_lib(b, g5, 0), group_lib(b, g5, 1));
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g5, i);
		int immediate_libs = immediate_liberty_count(b, lib);
		if (immediate_libs > 1 ||
		    (immediate_libs == 1 && !adjecent_libs))		/* Can escape */
			return false;

		int neighbors = neighbor_count_at(b, lib, other_color);
		if (neighbors > 2)					/* Bad shape or can connect out */
			return false;
		if (neighbors == 2)
			foreach_neighbor(b, lib, {
				if (board_at(b, c) == other_color &&
				    group_at(b, c) != g5)		/* Can connect out */
					return false;
			});
	}

	/* Check nakade shape is dead */
	mq_t area;  mq_init(&area);
	for (coord_t c = g5; c; c = groupnext_at(b, c))
		mq_add(&area, c);
	assert(area.moves == 5);
	if (!nakade_area_dead_shape(b, &area))
		return false;

	/* But alive with both extra moves. */
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g5, i);
		mq_add(&area, lib);
		if (nakade_area_dead_shape(b, &area))
			return false;
		area.moves--;  // faster mq_remove(&area, lib);
	}

	/* Check group is completely surrounded and surrounding groups are connected.
	 * Can't escape and can't connect out, only countercaptures left to check. */
	if (can_countercapture(b, g5, NULL) || !surrounding_groups_connected(b, g5))
		return false;

	/* Group alive after capturing these stones ? */
	bool safe = false;
	with_move_strict(b, coord, color, {
		assert(group_libs(b, g5) == 1);
		coord_t lib = group_lib(b, g5, 0);
		with_move_strict(b, lib, color, {
			group_t g = group_at(b, own);
			assert(g);  assert(!group_at(b, g5));
			safe = dragon_is_safe(b, g, color);
		});
	});
	return !safe;
}

/*   . . O O O |
 *   O O O . . |
 *   O X X X X |       We're black.
 *   O X O O . |
 *   O X O O O |       Are we about to break 6-stones seki by playing at @coord ?
 *   O X X O . |       Assumes selfatari checks passed, so we have some outside liberties.
 *   O . X X X |
 *   . O O O O |       */
static bool
breaking_6_stone_seki(board_t *b, coord_t coord, enum stone color, group_t own, group_t g6)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
	assert(board_at(b, coord) == S_NONE);
	assert(sane_group(b, own));
	assert(sane_group(b, g6));
	assert(group_libs(b, g6) == 2);
	assert(group_stone_count(b, g6, 7) == 6);
#endif
	enum stone other_color = stone_other(color);

	/* Check neighbours of the 2 liberties first.
	 * Not as simple as 3-stones / 4-stones case:
	 * Adjacent liberties and liberty in empty triangle are fine here. */
	bool adjecent_libs = coord_is_adjecent(group_lib(b, g6, 0), group_lib(b, g6, 1));
	for (int i = 0; i < 2; i++) {
		coord_t lib = group_lib(b, g6, i);
		int immediate_libs = immediate_liberty_count(b, lib);
		if (immediate_libs > 1 ||
		    (immediate_libs == 1 && !adjecent_libs))		/* Can escape */
			return false;

		int neighbors = neighbor_count_at(b, lib, other_color);
		if (neighbors > 2)					/* Bad shape or can connect out */
			return false;
		if (neighbors == 2)
			foreach_neighbor(b, lib, {
				if (board_at(b, c) == other_color &&
				    group_at(b, c) != g6)		/* Can connect out */
					return false;
			});
	}

	/* Check nakade shape is dead */
	mq_t area;  mq_init(&area);
	for (coord_t c = g6; c; c = groupnext_at(b, c))
		mq_add(&area, c);
	assert(area.moves == 6);
	if (!nakade_area_dead_shape(b, &area))
		return false;

	/* Check group is completely surrounded and surrounding groups are connected.
	 * Can't escape and can't connect out, only countercaptures left to check. */
	if (can_countercapture(b, g6, NULL) || !surrounding_groups_connected(b, g6))
		return false;

	/* Group alive after capturing these stones ? */
	bool safe = false;
	with_move_strict(b, coord, color, {
		assert(group_libs(b, g6) == 1);
		coord_t lib = group_lib(b, g6, 0);
		with_move_strict(b, lib, color, {
			group_t g = group_at(b, own);
			assert(g);  assert(!group_at(b, g6));
			safe = dragon_is_safe(b, g, color);
		});
	});
	return !safe;
}

/*   . O O O O |
 *   O . X X X |      We're black.
 *   O . X * O |
 *   O . X X O |      Are we about to break a nakade seki by playing at @coord ?
 *   O . X . O |      Assumes selfatari checks passed, so we have some outside liberties.
 *   O . X X X |
 *   . O O O O |      */
bool
breaking_nakade_seki(board_t *b, coord_t coord, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
	assert(board_at(b, coord) == S_NONE);
#endif
	enum stone other_color = stone_other(color);

	if (!neighbor_count_at(b, coord, color) ||
	    !neighbor_count_at(b, coord, other_color) ||
	    immediate_liberty_count(b, coord) > 1)
		return false;

	/* Checks groups nearby:
	 * There must be only one opponent group (2 libs, 3/4/5 stones)
	 * and at least one own group. */
	group_t group = 0, own = 0;
	int stones = 0;
	foreach_neighbor(b, coord, {
		group_t g = group_at(b, c);
		if (!g)  continue;

		/* Own group(s) */
		if (board_at(b, c) == color) {  own = g;  continue;  }

		/* Opponent group */
		if (group) {
			if (group != g)		return false;  /* Multiple groups */
							       /* Found same group twice (group == g) */
			if (stones <= 4)	return false;  /* Bad shape: empty triangle liberty, 3/4 stone group */
		}
		if (group_libs(b, g) != 2)	return false;
		stones = group_stone_count(b, g, 7);
		// Faster without ?
		if (stones < 3 || stones > 6)	return false;

		group = g;
	});

	/* Check around other lib */
	coord_t lib = group_other_lib(b, group, coord);
	if (!neighbor_count_at(b, lib, color) ||
	    immediate_liberty_count(b, lib) > 1)
		return false;

	switch (stones) {
		case 3:  return breaking_3_stone_seki(b, coord, color, own, group);
		case 4:  return breaking_4_stone_seki(b, coord, color, own, group);
		case 5:  return breaking_5_stone_seki(b, coord, color, own, group);
		case 6:  return breaking_6_stone_seki(b, coord, color, own, group);
	}

	return false;
}
