
#define DEBUG
#include "board.h"
#include "engine.h"
#include "pattern/spatial.h"
#include "josekifix/override.h"
#include "josekifix/joseki_override.h"


/*  6 | . . . . . . . 
 *  5 | . . . . . . .    3-3 invasion
 *  4 | . * . O . . .    If we own everything around here, try to kill.
 *  3 | . . X). . O . 
 *  2 | . . . . . . .    Ex:  t-regress/kill_3-3_invasion
 *  1 | . . . . . . . 
 *    +---------------
 *      A B C D E F G    */
coord_t
josekifix_kill_3_3_invasion(struct board *b, struct ownermap *prev_ownermap,
			    hash_t lasth)
{
	enum stone our_color = stone_other(last_move(b).color);
	override_t override = 	{ .coord = "B2", .prev = "C3", "B4", "", { 0xfb50710e59804023, 0x7fefef0db770bf17, 0xef77e916af17fb33, 0x255a0304dbe9fd17, 
									   0x41fad91638b3a0eb, 0xe04691d5dc8ef2f, 0x8e93b792ac2f9dfb, 0x79549dde6309036f } };
	int rot;
	coord_t c = check_override(b, &override, &rot, lasth, "joseki_override");
	if (is_pass(c)) return c;

	/* Corner and side owned by us in prev ownermap ?
	 * Check ownermap pattern around c = B4 */
	int cx = coord_x(c);    int cy = coord_y(c);
	for (unsigned int d = 2; d <= MAX_PATTERN_DIST; d++) {
		for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, cx, cy, j);
			coord_t c2 = coord_xy(x, y);
			if (board_at(b, c2) == S_OFFBOARD) continue;

			if (our_color != ownermap_color(prev_ownermap, c2, 0.67))
				return pass;
		}
	}

	josekifix_log("joseki_override: %s (%s)\n", coord2sstr(c), "kill 3-3 invasion");
	return c;
}


