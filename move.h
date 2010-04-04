#ifndef ZZGO_MOVE_H
#define ZZGO_MOVE_H

#include <stdint.h>
#include <string.h>

#include "util.h"
#include "stone.h"

typedef int coord_t;

#define coord_raw(c) (c)
#define coord_x(c, b) ((c) % board_size(b))
#define coord_y(c, b) ((c) / board_size(b))
#define coord_eq(c1, c2) ((c1) == (c2))
/* TODO: Smarter way to do this? */
#define coord_dx(c1, c2, b) (coord_x(c1, b) - coord_x(c2, b))
#define coord_dy(c1, c2, b) (coord_y(c1, b) - coord_y(c2, b))

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

struct board;
/* Return coordinate string in a dynamically allocated buffer. Thread-safe. */
char *coord2str(coord_t c, struct board *b);
/* Return coordinate string in a static buffer; multiple buffers are shuffled
 * to enable use for multiple printf() parameters, but it is NOT safe for
 * anything but debugging - in particular, it is NOT thread-safe! */
char *coord2sstr(coord_t c, struct board *b);
coord_t *str2coord(char *str, int board_size);


struct move {
	coord_t coord;
	enum stone color;
};



static inline coord_t *
coord_init(int x, int y, int size)
{
	coord_t *c = calloc2(1, sizeof(coord_t));
	*c = x + y * size;
	return c;
}

static inline coord_t *
coord_copy(coord_t c)
{
	coord_t *c2 = calloc2(1, sizeof(coord_t));
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
