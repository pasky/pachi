#ifndef PACHI_TACTICS_1LIB_H
#define PACHI_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "board.h"
#include "debug.h"

/* Can capture group g (not snapback) */
bool can_capture(board_t *b, group_t g, enum stone to_play);

/* Check if @group capture is a snapback.
 * see also is_snapback(): faster for checking a potential move than
 * with_move(selfatari) + capturing_group_is_snapback() */
bool capturing_group_is_snapback(board_t *b, group_t group);
/* Can group @group usefully capture a neighbor ? 
 * (usefully: not a snapback) */
bool can_countercapture(board_t *b, group_t group, mq_t *q);
/* Same as can_countercapture() but returns capturable groups instead of moves,
 * queue may not be NULL, and is always cleared. */
bool countercapturable_groups(board_t *b, group_t group, mq_t *q);
/* Can group @group capture *any* neighbor ? */
bool can_countercapture_any(board_t *b, group_t group, mq_t *q);

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(unsigned int alwaysccaprate, board_t *b, group_t group, enum stone to_play,
                       mq_t *q, bool middle_ladder);


/* Returns 0 or ID of neighboring group in atari. */
static group_t board_get_atari_neighbor(board_t *b, coord_t coord, enum stone group_color);
/* Get all neighboring groups in atari */
static void board_get_atari_neighbors(board_t *b, coord_t coord, enum stone group_color, mq_t *q);


static inline group_t
board_get_atari_neighbor(board_t *b, coord_t coord, enum stone group_color)
{
	assert(coord != pass);
	foreach_neighbor(b, coord, {
		if (board_at(b, c) != group_color)
			continue;
		group_t g = group_at(b, c);
		if (group_libs(b, g) == 1)
			return g;
		/* We return first match. */
	});
	return 0;
}

static inline void
board_get_atari_neighbors(board_t *b, coord_t c, enum stone group_color, mq_t *q)
{
	assert(c != pass);
	mq_init(q);
	foreach_neighbor(b, c, {
		if (board_at(b, c) != group_color)
			continue;
		group_t g = group_at(b, c);
		if (group_libs(b, g) == 1)
			mq_add_nodup(q, g);
	});
}

#define foreach_atari_neighbor(b, c, group_color)				\
	do {									\
		mq_t q__;							\
		board_get_atari_neighbors((b), (c), (group_color), &q__);	\
		for (int i__ = 0; i__ < q__.moves; i__++) {			\
			group_t g = q__.move[i__];

#define foreach_atari_neighbor_end  \
			} \
	} while (0)


#endif
