#ifndef ZZGO_TACTICS_LADDER_H
#define ZZGO_TACTICS_LADDER_H

/* Reading ladders. */

#include "board.h"
#include "debug.h"

/* Check if escaping on this liberty by given group in atari would play out
 * a simple ladder. */
/* Two ways of ladder reading can be enabled separately; simple first-line
 * ladders and trivial middle-board ladders. */
static bool is_ladder(struct board *b, coord_t coord, group_t laddered,
                      bool border_ladders, bool middle_ladders);


bool is_border_ladder(struct board *b, coord_t coord, enum stone lcolor);
bool is_middle_ladder(struct board *b, coord_t coord, enum stone lcolor);
static inline bool
is_ladder(struct board *b, coord_t coord, group_t laddered,
          bool border_ladders, bool middle_ladders)
{
	enum stone lcolor = board_at(b, group_base(laddered));

	if (DEBUGL(6))
		fprintf(stderr, "ladder check - does %s play out %s's laddered group %s?\n",
			coord2sstr(coord, b), stone2str(lcolor), coord2sstr(laddered, b));

	/* First, special-case first-line "ladders". This is a huge chunk
	 * of ladders we actually meet and want to play. */
	if (border_ladders
	    && neighbor_count_at(b, coord, S_OFFBOARD) == 1
	    && neighbor_count_at(b, coord, lcolor) == 1) {
		bool l = is_border_ladder(b, coord, lcolor);
		if (DEBUGL(6)) fprintf(stderr, "border ladder solution: %d\n", l);
		return l;
	}

	if (middle_ladders) {
		bool l = is_middle_ladder(b, coord, lcolor);
		if (DEBUGL(6)) fprintf(stderr, "middle ladder solution: %d\n", l);
		return l;
	}

	if (DEBUGL(6)) fprintf(stderr, "no ladder to be checked\n");
	return false;
}

#endif
