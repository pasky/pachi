#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

//#define DEBUG
#include "board.h"
#include "debug.h"
#include "move.h"
#include "tactics/nakade.h"


static inline int
nakade_area(board_t *b, coord_t around, enum stone color, mq_t *area)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(around));
	assert(is_player_color(color));
	assert(board_at(b, around) == S_NONE);
#endif
	/* First, examine the nakade area. For sure, it must be at most
	 * six points. And it must be within color group(s). */
#define NAKADE_MAX 6
	mq_init(area);

	mq_add(area, around);

	for (int i = 0; i < area->moves; i++) {
		foreach_neighbor(b, area->move[i], {
			if (board_at(b, c) == stone_other(color))
				return -1;
			if (board_at(b, c) == S_NONE) {
				mq_add_nodup(area, c);
				if (area->moves > NAKADE_MAX)  /* Too large nakade area. */
					return -1;
			}
		});
	}

	return area->moves;
}

static inline void
get_neighbors(board_t *b, mq_t *area, int *neighbors, int *ptbynei)
{
	/* We also collect adjecency information - how many neighbors
	 * we have for each area point, and histogram of this. This helps
	 * us verify the appropriate bulkiness of the shape. */
	int area_n = area->moves;
        memset(neighbors, 0, area_n * sizeof(int));
	for (int i = 0; i < area_n; i++) {
		for (int j = i + 1; j < area_n; j++)
			if (coord_is_adjecent(area->move[i], area->move[j])) {
				ptbynei[neighbors[i]]--;
				neighbors[i]++;
				ptbynei[neighbors[i]]++;
				ptbynei[neighbors[j]]--;
				neighbors[j]++;
				ptbynei[neighbors[j]]++;
			}
	}
}

static inline coord_t
nakade_point_(mq_t *area, int *neighbors, int *ptbynei)
{
	/* For each given neighbor count, arbitrary one coordinate
	 * featuring that. */
	coord_t coordbynei[9];
	for (int i = 0; i < area->moves; i++)
		coordbynei[neighbors[i]] = area->move[i];

	switch (area->moves) {
		case 1: return pass;
		case 2: return pass;
		case 3: assert(ptbynei[2] == 1);
			return coordbynei[2]; // middle point
		case 4: if (ptbynei[3] != 1) return pass; // long line, L shape, or square
			return coordbynei[3]; // tetris four
		case 5: if (ptbynei[3] == 1 && ptbynei[1] == 1) return coordbynei[3]; // bulky five
			if (ptbynei[4] == 1) return coordbynei[4]; // cross five
			return pass; // long line
		case 6: if (ptbynei[4] == 1 && ptbynei[2] == 3)
				return coordbynei[4]; // rabbity six
			return pass; // anything else
		default: assert(0);
	}

	return 0; /* NOTREACHED */
}

coord_t
nakade_point(board_t *b, coord_t around, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(around));
	assert(is_player_color(color));
	assert(board_at(b, around) == S_NONE);
#endif
	mq_t area;
	int area_n = nakade_area(b, around, color, &area);
	if (area_n == -1)
		return pass;

	int neighbors[area_n]; int ptbynei[9] = {area_n, 0};
	get_neighbors(b, &area, neighbors, ptbynei);
	
	return nakade_point_(&area, neighbors, ptbynei);
}

bool
nakade_area_dead_shape(board_t *b, mq_t *area)
{
	int area_n = area->moves;
	if (area_n <= 3)		return true;
	if (area_n > NAKADE_MAX)	return false;

	int neighbors[area_n]; int ptbynei[9] = {area_n, 0};
	get_neighbors(b, area, neighbors, ptbynei);

	if (area_n == 4 && ptbynei[2] == 4)  // square 4
		return true;

	/* nakade_point() should be able to deal with the rest ... */
	coord_t nakade = nakade_point_(area, neighbors, ptbynei);
	return  (nakade != pass);
}

bool
nakade_dead_shape(board_t *b, coord_t around, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(around));
	assert(is_player_color(color));
	assert(board_at(b, around) == S_NONE);
#endif
	mq_t area;
	int area_n = nakade_area(b, around, color, &area);
	if (area_n == -1)
		return false;
	return nakade_area_dead_shape(b, &area);
}
