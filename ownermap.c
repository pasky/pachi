#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "mq.h"
#include "ownermap.h"


void
board_ownermap_fill(struct board_ownermap *ownermap, struct board *b)
{
	ownermap->playouts++;
	foreach_point(b) {
		enum stone color = board_at(b, c);
		if (color == S_NONE)
			color = board_get_one_point_eye(b, c);
		ownermap->map[c][color]++;
	} foreach_point_end;
}

void
board_ownermap_merge(int bsize2, struct board_ownermap *dst, struct board_ownermap *src)
{
	dst->playouts += src->playouts;
	for (int i = 0; i < bsize2; i++)
		for (int j = 0; j < S_MAX; j++)
			dst->map[i][j] += src->map[i][j];
}

enum point_judgement
board_ownermap_judge_point(struct board_ownermap *ownermap, coord_t c, floating_t thres)
{
	assert(ownermap->map);
	int n = ownermap->map[c][S_NONE];
	int b = ownermap->map[c][S_BLACK];
	int w = ownermap->map[c][S_WHITE];
	int total = ownermap->playouts;
	if (n >= total * thres)
		return PJ_DAME;
	else if (n + b >= total * thres)
		return PJ_BLACK;
	else if (n + w >= total * thres)
		return PJ_WHITE;
	else
		return PJ_UNKNOWN;
}

void
board_ownermap_judge_groups(struct board *b, struct board_ownermap *ownermap, struct group_judgement *judge)
{
	assert(ownermap->map);
	assert(judge->gs);
	memset(judge->gs, GS_NONE, board_size2(b) * sizeof(judge->gs[0]));

	foreach_point(b) {
		enum stone color = board_at(b, c);
		group_t g = group_at(b, c);
		if (!g) continue;

		enum point_judgement pj = board_ownermap_judge_point(ownermap, c, judge->thres);
		// assert(judge->gs[g] == GS_NONE || judge->gs[g] == pj);
		if (pj == PJ_UNKNOWN) {
			/* Fate is uncertain. */
			judge->gs[g] = GS_UNKNOWN;

		} else if (judge->gs[g] != GS_UNKNOWN) {
			/* Update group state. */
			enum gj_state new;

			// Comparing enum types, casting (int) avoids compiler warnings
			if ((int)pj == (int)color) { 
				new = GS_ALIVE;
			} else if ((int)pj == (int)stone_other(color)) {
				new = GS_DEAD;
			} else { assert(pj == PJ_DAME);
				/* Exotic! */
				new = GS_UNKNOWN;
			}

			if (judge->gs[g] == GS_NONE) {
				judge->gs[g] = new;
			} else if (judge->gs[g] != new) {
				/* Contradiction. :( */
				judge->gs[g] = GS_UNKNOWN;
			}
		}
	} foreach_point_end;
}

void
groups_of_status(struct board *b, struct group_judgement *judge, enum gj_state s, struct move_queue *mq)
{
	foreach_point(b) { /* foreach_group, effectively */
		group_t g = group_at(b, c);
		if (!g || g != c) continue;

		assert(judge->gs[g] != GS_NONE);
		if (judge->gs[g] == s)
			mq_add(mq, g, 0);
	} foreach_point_end;
}
