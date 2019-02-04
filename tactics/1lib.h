#ifndef PACHI_TACTICS_1LIB_H
#define PACHI_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "board.h"
#include "debug.h"

struct move_queue;


bool capturing_group_is_snapback(struct board *b, group_t group);
/* Can group @group usefully capture a neighbor ? 
 * (usefully: not a snapback) */
bool can_countercapture(struct board *b, group_t group, struct move_queue *q, int tag);
/* Same as can_countercapture() but returns capturable groups instead of moves,
 * queue may not be NULL, and is always cleared. */
bool countercapturable_groups(struct board *b, group_t group, struct move_queue *q);
/* Can group @group capture *any* neighbor ? */
bool can_countercapture_any(struct board *b, group_t group, struct move_queue *q, int tag);

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(unsigned int alwaysccaprate, struct board *b, group_t group, enum stone to_play,
                       struct move_queue *q, coord_t *ladder, bool middle_ladder, int tag);


/* Returns 0 or ID of neighboring group in atari. */
static group_t board_get_atari_neighbor(struct board *b, coord_t coord, enum stone group_color);
/* Get all neighboring groups in atari */
static void board_get_atari_neighbors(struct board *b, coord_t coord, enum stone group_color, struct move_queue *q);


static inline group_t
board_get_atari_neighbor(struct board *b, coord_t coord, enum stone group_color)
{
	assert(coord != pass);
	foreach_neighbor(b, coord, {
		group_t g = group_at(b, c);
		if (g && board_at(b, c) == group_color && board_group_info(b, g).libs == 1)
			return g;
		/* We return first match. */
	});
	return 0;
}

static inline void
board_get_atari_neighbors(struct board *b, coord_t c, enum stone group_color, struct move_queue *q)
{
	assert(c != pass);
	q->moves = 0;
	foreach_neighbor(b, c, {
		group_t g = group_at(b, c);
		if (g && board_at(b, c) == group_color && board_group_info(b, g).libs == 1) {
			mq_add(q, g, 0);
			mq_nodup(q);
		}
	});
}

#define foreach_atari_neighbor(b, c, group_color)			\
	do {								\
		struct move_queue __q;					\
		board_get_atari_neighbors(b, (c), (group_color), &__q);	\
		for (unsigned int __i = 0; __i < __q.moves; __i++) {		\
			group_t g = __q.move[__i];

#define foreach_atari_neighbor_end  \
			} \
	} while (0)


#endif
