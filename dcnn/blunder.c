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

/* Prevent silly first-line connect blunders where group can be captured afterwards */
static bool
dcnn_first_line_connect_blunder(board_t *b, move_t *m)
{
	/* First-line connect blunder ? */
	if (coord_edge_distance(m->coord) != 0)  return false;
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

	return false;
}

/*  7 | . . . . . . . . 
    6 | . . . X . . . .    Prevent w B3 and C1 blunders, happens sometimes in handicap games
    5 | . . . . . . . .
    4 | . . X X . O . .    w wants to play B2 later here (endgame)
    3 | . . O X X O . .
    2 | . . X O O . . . 
    1 | . . . . . . . .    Ex:  t-unit/dcnn_blunder.t
      +-----------------        t-regress/4-4_reduce_3-3
        A B C D E F G H    */
static bool
dcnn_44_reduce_33_blunder(board_t *b, move_t *m, move_t *redirect)
{
	/* B3 not blunder if w has a stone at B6, make sure override covers that area. */
	override_t override = { .coord_empty = "B1", .prev = "pass", "B2", "4-4 reduce 3-3", { 0x104718b6711a28d0, 0xdcd0e566177a90e8, 0xb1256f54939c1c48, 0xce86cd889eb98e38,
											       0xd39f04865100718, 0x8dfd49f239c658, 0xa84411bdaafa8a10, 0xbd0cd1b2a8ace9b8 } };
	int dist = coord_edge_distance(m->coord);
	if (dist != 1 && dist != 0)  return false;

	/* Check if m is like w B3 or C1 */
	coord_t b3 = str2coord("B3");
	coord_t c1 = str2coord("C1");
	for (int rot = 0; rot < 8; rot++) {
		coord_t rb3 = rotate_coord(b3, rot);
		coord_t rc1 = rotate_coord(c1, rot);
		
		/* Check we're in the right quadrant for m */
		if (m->coord != rb3 && m->coord != rc1)  continue;
		
		coord_t c = check_override_rot(b, &override, rot, 0);
		if (!is_pass(c)) {		/* Would rather just clobber since w doesn't want to play B2 right away, */
			redirect->coord = c;	/* but mcts ends up playing B3 anyway sometimes in this case ! */
			return true;		/* So redirect, if it had a big prior will play B2 right away, */
		}				/* no big deal. */
	}

	return false;
}

/* Check if move m is a dcnn blunder.
 * Return true:                 clobber move
 * Return true + set redirect:  redirect dcnn prior and clobber move */
static bool
dcnn_blunder(board_t *b, move_t *m, float r, move_t *redirect)
{
	if (r < 0.01)  return false;
	if (board_playing_ko_threat(b))  return false;
	
	if (dcnn_first_line_connect_blunder(b, m))      return true;
	if (dcnn_44_reduce_33_blunder(b, m, redirect))  return true;
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
		move_t redirect = move(pass, color);
		
		if (dcnn_blunder(b, &m, result[k], &redirect)) {
			if (redirect.coord != pass) {		/* redirect + clobber */
				int k2 = coord2dcnn_idx(redirect.coord);
				result[k2] += result[k];
				if (debugl)  fprintf(stderr, "dcnn blunder: replaced %-3s -> %-3s  (%i%%)\n",
						     coord2sstr(c), coord2sstr(redirect.coord), (int)(result[k] * 100));
			}
			else					/* clobber */
				if (debugl)  fprintf(stderr, "dcnn blunder: fixed %-3s  %i%% -> %i%%\n",
						     coord2sstr(c), (int)(result[k] * 100), (int)(blunder_rating * 100));
			
			result[k] = blunder_rating;
		}
	} foreach_free_point_end;
}
