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

	enum stone *b; /* stones */
	int *g; /* group ids */

	/* private */
	int last_gid;
	bool *libcount_watermark;
};

#define board_atxy(b_, x, y) (b_->b[x + b_->size * y])
#define board_at(b_, c) board_atxy(b_, c.x, c.y)

#define group_atxy(b_, x, y) (b_->g[x + b_->size * y])
#define group_at(b_, c) group_atxy(b_, c.x, c.y)

struct board *board_init(void);
struct board *board_copy(struct board *board);
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

int board_group_libs(struct board *board, int group);
void board_group_capture(struct board *board, int group);

/* Positive: W wins */
float board_count_score(struct board *board);

#endif
