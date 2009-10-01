#ifndef ZZGO_MOVE_H
#define ZZGO_MOVE_H

#include <stdint.h>
#include <string.h>

#include "stone.h"

typedef int coord_t;
struct board;

#define coord_raw(c) (c)
#define coord_x(c, b) ((c) % board_size(b))
#define coord_y(c, b) ((c) / board_size(b))
#define coord_eq(c1, c2) ((c1) == (c2))

static coord_t pass = -1;
static coord_t resign = -2;
#define is_pass(c) (coord_eq(c, pass))
#define is_resign(c) (coord_eq(c, resign))

/* Initialize existing coord */
#define coord_pos(coord, pos_, board) do { (coord) = (pos_); } while (0)
#define coord_xy(board, x, y) ((x) + (y) * board_size(board))
#define coord_xy_otf(x, y, board) coord_xy(board, x, y) // obsolete

#define coord_is_adjecent(c1, c2, b) (abs(c1 - c2) == 1 || abs(c1 - c2) == board_size(b))
#define coord_is_8adjecent(c1, c2, b) (abs(c1 - c2) == 1 || abs(abs(c1 - c2) - board_size(b)) < 2)

/* dyn allocated */
static coord_t *coord_init(int x, int y, int size);
static coord_t *coord_copy(coord_t c);
static coord_t *coord_pass(void);
static coord_t *coord_resign(void);
static void coord_done(coord_t *c);
int coord_edge_distance(coord_t c, struct board *b);

struct board;
char *coord2str(coord_t c, struct board *b);
char *coord2sstr(coord_t c, struct board *b);
coord_t *str2coord(char *str, int board_size);


struct move {
	coord_t coord;
	enum stone color;
};



static inline coord_t *
coord_init(int x, int y, int size)
{
	coord_t *c = calloc(1, sizeof(coord_t));
	*c = x + y * size;
	return c;
}

static inline coord_t *
coord_copy(coord_t c)
{
	coord_t *c2 = calloc(1, sizeof(coord_t));
	memcpy(c2, &c, sizeof(c));
	return c2;
}

static inline coord_t *
coord_pass()
{
	return coord_copy(pass);
}

static inline coord_t *
coord_resign()
{
	return coord_copy(resign);
}

static inline void
coord_done(coord_t *c)
{
	free(c);
}

#endif
