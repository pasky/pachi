#ifndef ZZGO_BOARD_H
#define ZZGO_BOARD_H

#include <stdbool.h>

#include "stone.h"
#include "move.h"


#define MAXSIZE 19

struct board {
	int size;
	int captures[S_MAX];
	float komi;

	int moves;
	struct move last_move;

	enum stone *b; /* map of stones */
	int *g; /* map of group ids; 0 == no group */
	int *g_libs; /* cache of group liberties; indexed by gid */

	/* private */
	int last_gid;
	bool *libcount_watermark;
};

#define board_atxy(b_, x, y) ((b_)->b[(x) + (b_)->size * (y)])
#define board_at(b_, c) board_atxy(b_, (c).x, (c).y)

#define group_atxy(b_, x, y) ((b_)->g[x + (b_)->size * (y)])
#define group_at(b_, c) group_atxy(b_, (c).x, (c).y)

#define board_group_libs(b_, g_) ((b_)->g_libs[(g_)])

struct board *board_init(void);
struct board *board_copy(struct board *board2, struct board *board1);
void board_done_noalloc(struct board *board);
void board_done(struct board *board);
void board_resize(struct board *board, int size);
void board_clear(struct board *board);

struct FILE;
void board_print(struct board *board, FILE *f);

/* Returns group id */
int board_play(struct board *board, struct move *m);

bool board_no_valid_moves(struct board *board, enum stone color);
bool board_valid_move(struct board *board, struct move *m, bool sensible);

/* Local liberties of a position - adj. positions with no
 * stones on them. (Thus, even stones in alive group may have
 * zero local liberties.) */
int board_local_libs(struct board *board, struct coord *c);

int board_group_libs_recount(struct board *board, int group);
void board_group_capture(struct board *board, int group);

/* Positive: W wins */
/* board_official_score() is the scoring method for yielding score suitable
 * for external presentation. For fast scoring of two ZZGos playing,
 * use board_fast_score(). */
float board_official_score(struct board *board);
float board_fast_score(struct board *board);


/** Iterators */

#define foreach_point(board_) \
	do { \
		int x, y; \
		for (x = 0; x < board_->size; x++) { \
			for (y = 0; y < board_->size; y++) { \
				struct coord c = { x, y }; c = c; /* shut up gcc */
#define foreach_point_end \
			} \
		} \
	} while (0)

#define foreach_in_group(board_, group_) \
	do { \
		int group__ = group_; \
		foreach_point(board_) \
			if (group_at(board_, c) == group__)
#define foreach_in_group_end \
		foreach_point_end; \
	} while (0)

#define foreach_neighbor(board_, coord_) \
	do { \
		struct coord q__[] = { { (coord_).x - 1, (coord_).y }, \
		                       { (coord_).x, (coord_).y - 1 }, \
		                       { (coord_).x + 1, (coord_).y }, \
		                       { (coord_).x, (coord_).y + 1 } }; \
		int fn__i; \
		for (fn__i = 0; fn__i < 4; fn__i++) { \
			int x = q__[fn__i].x, y = q__[fn__i].y; struct coord c = { x, y }; \
			if (x < 0 || y < 0 || x >= board_->size || y >= board->size) \
				continue;
#define foreach_neighbor_end \
		} \
	} while (0)


#endif
