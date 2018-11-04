#ifndef PACHI_MOVE_H
#define PACHI_MOVE_H

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "util.h"
#include "stone.h"

typedef int coord_t;

#define coord_xy(board, x, y) ((x) + (y) * board_size(board))
#define coord_x(c, b) (board_statics.coord[c][0])
#define coord_y(c, b) (board_statics.coord[c][1])
/* TODO: Smarter way to do this? */
#define coord_dx(c1, c2, b) (coord_x(c1, b) - coord_x(c2, b))
#define coord_dy(c1, c2, b) (coord_y(c1, b) - coord_y(c2, b))

#define pass   -1
#define resign -2
#define is_pass(c)   (c == pass)
#define is_resign(c) (c == resign)

#define coord_is_adjecent(c1, c2, b) (abs(c1 - c2) == 1 || abs(c1 - c2) == board_size(b))
#define coord_is_8adjecent(c1, c2, b) (abs(c1 - c2) == 1 || abs(abs(c1 - c2) - board_size(b)) < 2)

struct board;
char *coord2bstr(char *buf, coord_t c, struct board *board);
/* Return coordinate string in a dynamically allocated buffer. Thread-safe. */
char *coord2str(coord_t c, struct board *b);
/* Return coordinate string in a static buffer; multiple buffers are shuffled
 * to enable use for multiple printf() parameters, but it is NOT safe for
 * anything but debugging - in particular, it is NOT thread-safe! */
char *coord2sstr(coord_t c, struct board *b);
coord_t str2coord(char *str, int board_size);
/* Rotate coordinate according to rot: [0-7] for 8 board symmetries. */
coord_t rotate_coord(struct board *b, coord_t c, int rot);

struct move {
	coord_t coord;
	enum stone color;
};


static inline int 
move_cmp(struct move *m1, struct move *m2)
{
	if (m1->color != m2->color)
		return m1->color - m2->color;
	return m1->coord - m2->coord;
}

#endif
