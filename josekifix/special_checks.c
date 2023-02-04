
#define DEBUG
#include "board.h"
#include "engine.h"
#include "pattern/spatial.h"
#include "josekifix/josekifix.h"


/*  6 | . . . . . . . 
 *  5 | . . . . . . .    3-3 invasion
 *  4 | . * . O . . .    If we own everything around here, try to kill.
 *  3 | . . X). . O . 
 *  2 | . . . . . . .    Ex: pachipachi hidek  2018-03-15
 *  1 | . . . . . . . 
 *    +---------------
 *      A B C D E F G
 */

coord_t
josekifix_kill_3_3_invasion(struct board *b, struct ownermap *prev_ownermap,
			    hash_t lasth)
{
	override_t override = 	{ .coord_empty = "B2", .prev = "C3", "B4", "", { 0xfb50710e59804023, 0x7fefef0db770bf17, 0xef77e916af17fb33, 0x255a0304dbe9fd17, 
										 0x41fad91638b3a0eb, 0xe04691d5dc8ef2f, 0x8e93b792ac2f9dfb, 0x79549dde6309036f } };
	int rot;
	coord_t c = check_override(b, &override, &rot, lasth);
	if (is_pass(c)) return c;
	
	/* Corner owned by us in prev ownermap ? */
	enum stone our_color = stone_other(last_move(b).color);
	coord_t b4 = str2coord("B4");
	coord_t rb4 = rotate_coord(b4, rot);
	int cx = coord_x(rb4);    int cy = coord_y(rb4);
	
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


