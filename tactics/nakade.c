#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "move.h"
#include "tactics/nakade.h"


coord_t
nakade_point(struct board *b, coord_t around, enum stone color)
{
	/* First, examine the nakade area. For sure, it must be at most
	 * six points. And it must be within color group(s). */
#define NAKADE_MAX 6
	coord_t area[NAKADE_MAX]; int area_n = 0;

	area[area_n++] = around;

	for (int i = 0; i < area_n; i++) {
		foreach_neighbor(b, area[i], {
			if (board_at(b, c) == stone_other(color))
				return pass;
			if (board_at(b, c) == S_NONE) {
				bool dup = false;
				for (int j = 0; j < area_n; j++)
					if (c == area[j]) {
						dup = true;
						break;
					}
				if (dup) continue;

				if (area_n >= NAKADE_MAX) {
					/* Too large nakade area. */
					return pass;
				}
				area[area_n++] = c;
			}
		});
	}

	/* We also collect adjecency information - how many neighbors
	 * we have for each area point, and histogram of this. This helps
	 * us verify the appropriate bulkiness of the shape. */
	int neighbors[area_n]; int ptbynei[9] = {area_n, 0};
	memset(neighbors, 0, sizeof(neighbors));
	for (int i = 0; i < area_n; i++) {
		for (int j = i + 1; j < area_n; j++)
			if (coord_is_adjecent(area[i], area[j], b)) {
				ptbynei[neighbors[i]]--;
				neighbors[i]++;
				ptbynei[neighbors[i]]++;
				ptbynei[neighbors[j]]--;
				neighbors[j]++;
				ptbynei[neighbors[j]]++;
			}
	}

	/* For each given neighbor count, arbitrary one coordinate
	 * featuring that. */
	coord_t coordbynei[9];
	for (int i = 0; i < area_n; i++)
		coordbynei[neighbors[i]] = area[i];

	switch (area_n) {
		case 1: return pass;
		case 2: return pass;
		case 3: assert(ptbynei[2] == 1);
			return coordbynei[2]; // middle point
		case 4: if (ptbynei[3] != 1) return pass; // long line
			return coordbynei[3]; // tetris four
		case 5: if (ptbynei[3] == 1 && ptbynei[1] == 1) return coordbynei[3]; // bulky five
			if (ptbynei[4] == 1) return coordbynei[4]; // cross five
			return pass; // long line
		case 6: if (ptbynei[4] == 1 && ptbynei[2] == 3)
				return coordbynei[4]; // rabbity six
			return pass; // anything else
		default: assert(0);
	}
}
