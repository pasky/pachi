#ifndef PACHI_MOVE_H
#define PACHI_MOVE_H

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "util.h"
#include "stone.h"

typedef int coord_t;

#define coord_xy(board, x, y) ((x) + (y) * board_size(board))
#define coord_x(c, b) ((b)->coord[c][0])
#define coord_y(c, b) ((b)->coord[c][1])
/* TODO: Smarter way to do this? */
#define coord_dx(c1, c2, b) (coord_x(c1, b) - coord_x(c2, b))
#define coord_dy(c1, c2, b) (coord_y(c1, b) - coord_y(c2, b))

static coord_t pass = -1;
static coord_t resign = -2;
#define is_pass(c) (c == pass)
#define is_resign(c) (c == resign)

#define coord_is_adjecent(c1, c2, b) (abs(c1 - c2) == 1 || abs(c1 - c2) == board_size(b))
#define coord_is_8adjecent(c1, c2, b) (abs(c1 - c2) == 1 || abs(abs(c1 - c2) - board_size(b)) < 2)

/* Quadrants:
 * 0 1
 * 2 3 (vertically reversed from board_print output, of course!)
 * Middle coordinates are included in lower-valued quadrants. */
#define coord_quadrant(c, b) ((coord_x(c, b) > board_size(b) / 2) + 2 * (coord_y(c, b) > board_size(b) / 2))

/* dyn allocated */
static coord_t *coord_init(int x, int y, int size);
static coord_t *coord_copy(coord_t c);
static coord_t *coord_pass(void);
static coord_t *coord_resign(void);
static void coord_done(coord_t *c);

struct board;
char *coord2bstr(char *buf, coord_t c, struct board *board);
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
	coord_t *c = (coord_t*)calloc2(1, sizeof(coord_t));
	*c = x + y * size;
	return c;
}

static inline coord_t *
coord_copy(coord_t c)
{
	coord_t *c2 = (coord_t*)calloc2(1, sizeof(coord_t));
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

/* No sanity checking */
static inline coord_t
str2scoord(char *str, int size)
{
	if (!strcasecmp(str, "pass")) {
		return pass;
	} else if (!strcasecmp(str, "resign")) {
		return resign;
	} else {
		char xc = tolower(str[0]);
		return xc - 'a' - (xc > 'i') + 1 + atoi(str + 1) * size;
	}
}

static inline void
coord_done(coord_t *c)
{
	free(c);
}

#endif
