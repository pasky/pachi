#ifndef PACHI_OWNERMAP_H
#define PACHI_OWNERMAP_H

/* Map of board intersection owners, and devices to derive group status
 * information from the map. */

#include <signal.h> // sig_atomic_t

struct board_ownermap {
	/* Map of final owners of all intersections on the board. */
	/* This may be shared between multiple threads! */
	/* XXX: We assume sig_atomic_t is thread-atomic. This may not
	 * be true in pathological cases. */
	sig_atomic_t playouts;
	/* At the final board position, for each coordinate increase the
	 * counter of appropriate color. */
	sig_atomic_t (*map)[S_MAX]; // [board_size2(b)]
};

void board_ownermap_fill(struct board_ownermap *ownermap, struct board *b);
void board_ownermap_merge(int bsize2, struct board_ownermap *dst, struct board_ownermap *src);


/* Estimate coord ownership based on ownermap stats. */
enum point_judgement {
	PJ_DAME = S_NONE,
	PJ_BLACK = S_BLACK,
	PJ_WHITE = S_WHITE,
	PJ_UNKNOWN = 3,
};
enum point_judgement board_ownermap_judge_point(struct board_ownermap *ownermap, coord_t c, floating_t thres);


/* Estimate status of stones on board based on ownermap stats. */
struct group_judgement {
	floating_t thres;
	enum gj_state {
		GS_NONE,
		GS_DEAD,
		GS_ALIVE,
		GS_UNKNOWN,
	} *gs; // [bsize2]
};
void board_ownermap_judge_groups(struct board *b, struct board_ownermap *ownermap, struct group_judgement *judge);

/* Add groups of given status to mq. */
struct move_queue;
void groups_of_status(struct board *b, struct group_judgement *judge, enum gj_state s, struct move_queue *mq);

#endif
