#define DEBUG
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "dcnn.h"
#include "pattern/pattern.h"
#include "tactics/util.h"
#include "tactics/2lib.h"
#include "board_undo.h"
#include "josekifix/josekifix.h"

static bool
dcnn_blunder(board_t *b, move_t *m, float r)
{
	if (r < 0.01)  return false;
	if (board_playing_ko_threat(b))  return false;

	/* first-line connect blunder ? */
	if (coord_edge_distance(m->coord) == 0) {
		with_move(b, m->coord, m->color, {
			group_t g = group_at(b, m->coord);
			if (!g)  break;
			if (group_stone_count(b, g, 4) < 3)  break;
			
			/*   # . * .
			 *   # . O X     really stupid first-line connect blunder:
			 *   # O)O X     can capture right away
			 *   # O X X
			 *   # X . .
			 *   # . . .     */
			if (board_group_info(b, g).libs == 2 && can_capture_2lib_group(b, g, NULL, 0))
				with_move_return(true);

			/* 3 libs case */
			if (board_group_info(b, g).libs != 3)  break;
			for (int i = 0; i < board_group_info(b, g).libs; i++) {
				coord_t c = board_group_info(b, g).lib[i];
				move_t m2 = move(c, stone_other(m->color));
				if (pattern_match_l1_blunder_punish(b, &m2) != -1)
					with_move_return(true);
			}
		});
	}
	
	return false;
}

/* Fix dcnn blunders by altering dcnn priors before they get used.
 * (last resort, for moves which are a bad fit for a joseki override) */
void
dcnn_fix_blunders(board_t *b, enum stone color, float result[], bool debugl)
{
	float blunder_rating = 0.005;  /* 0.5% */
	
	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		move_t m = move(c, color);
		if (dcnn_blunder(b, &m, result[k])) {
			if (debugl)  fprintf(stderr, "fixed dcnn blunder %s: %i%% -> %i%%\n",
					     coord2sstr(c), (int)(result[k] * 100), (int)(blunder_rating * 100));
			result[k] = blunder_rating;
		}
	} foreach_free_point_end;
}
