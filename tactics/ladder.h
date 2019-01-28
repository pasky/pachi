#ifndef PACHI_TACTICS_LADDER_H
#define PACHI_TACTICS_LADDER_H

/* Reading ladders. */

#include "board.h"
#include "debug.h"

/* Check if group in atari can be caught in a ladder.
 * Two ways of ladder reading can be enabled separately; simple first-line
 * ladders and trivial middle-board ladders. */
static bool is_ladder(struct board *b, group_t laddered, bool test_middle);

/* Also consider unusual/very short ladders.
 * Note: same as is_ladder() if test_middle is false ... */
static bool is_ladder_any(struct board *b, group_t laddered, bool test_middle);

/* Check if a 2-lib group would be caught in a ladder given opponent stone at @chaselib. */
bool wouldbe_ladder(struct board *b, group_t group, coord_t chaselib);

/* Like wouldbe_ladder() but also consider unusual/very short ladders.
 * Use this if you only care whether group can be captured. */
bool wouldbe_ladder_any(struct board *b, group_t group, coord_t chaselib);

/* Try to find out if playing out lost ladder could be useful for life&death. 
 * Call right after is_ladder(), uses static state. */
bool useful_ladder(struct board *b, group_t laddered);

/* Playing out non-working ladder and getting ugly ? */
bool harmful_ladder_atari(struct board *b, coord_t atari, enum stone color);

bool is_border_ladder(struct board *b, group_t laddered);
bool is_middle_ladder(struct board *b, group_t laddered);
bool is_middle_ladder_any(struct board *b, group_t laddered);
static inline bool
is_ladder(struct board *b, group_t laddered, bool test_middle)
{
	assert(laddered);
	assert(group_at(b, laddered) == laddered);
	assert(board_group_info(b, laddered).libs == 1);

	if (DEBUGL(6)) {
		coord_t coord = board_group_info(b, laddered).lib[0];
		enum stone lcolor = board_at(b, group_base(laddered));
		fprintf(stderr, "ladder check - does %s play out %s's laddered group %s?\n",
			coord2sstr(coord), stone2str(lcolor), coord2sstr(laddered));
	}

	if (!test_middle) {
		/* First, special-case first-line "ladders". This is a huge chunk
		 * of ladders we actually meet and want to play. */
		coord_t coord = board_group_info(b, laddered).lib[0];
		enum stone lcolor = board_at(b, group_base(laddered));
		if (neighbor_count_at(b, coord, S_OFFBOARD) == 1
		    && neighbor_count_at(b, coord, lcolor) == 1) {
			bool l = is_border_ladder(b, laddered);
			if (DEBUGL(6)) fprintf(stderr, "border ladder solution: %d\n", l);
			return l;
		}
	}
	
	bool l = test_middle && is_middle_ladder(b, laddered);
	if (DEBUGL(6)) fprintf(stderr, "middle ladder solution: %d\n", l);
	return l;
}

static inline bool
is_ladder_any(struct board *b, group_t laddered, bool test_middle)
{
	assert(laddered);
	assert(group_at(b, laddered) == laddered);
	assert(board_group_info(b, laddered).libs == 1);

	if (DEBUGL(6)) {
		coord_t coord = board_group_info(b, laddered).lib[0];
		enum stone lcolor = board_at(b, group_base(laddered));
		fprintf(stderr, "ladder check - does %s play out %s's laddered group %s?\n",
			coord2sstr(coord), stone2str(lcolor), coord2sstr(laddered));
	}

	if (!test_middle) {
		/* First, special-case first-line "ladders". This is a huge chunk
		 * of ladders we actually meet and want to play. */
		coord_t coord = board_group_info(b, laddered).lib[0];
		enum stone lcolor = board_at(b, group_base(laddered));
		if (neighbor_count_at(b, coord, S_OFFBOARD) == 1
		    && neighbor_count_at(b, coord, lcolor) == 1) {
			bool l = is_border_ladder(b, laddered);
			if (DEBUGL(6)) fprintf(stderr, "border ladder solution: %d\n", l);
			return l;
		}
	}

	bool l = test_middle && is_middle_ladder_any(b, laddered);
	if (DEBUGL(6)) fprintf(stderr, "middle ladder solution: %d\n", l);
	return l;
}

#endif
