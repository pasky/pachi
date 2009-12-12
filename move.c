#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "move.h"


/* The S_OFFBOARD margin is not addressable by coordinates. */

static char asdf[] = "abcdefghjklmnopqrstuvwxyz";

char *
coord2str(coord_t c, struct board *board)
{
	return strdup(coord2sstr(c, board));
}

char *
coord2sstr(coord_t c, struct board *board)
{
	static char *b;
	static char bl[10][4];
	static int bi;
	if (is_pass(c)) {
		return "pass";
	} else if (is_resign(c)) {
		return "resign";
	} else {
		/* Some GTP servers are broken and won't grok lowercase coords */
		b = bl[bi]; bi = (bi + 1) % 10;
		snprintf(b, 4, "%c%d", toupper(asdf[coord_x(c, board) - 1]), coord_y(c, board));
		return b;
	}
}

/* No sanity checking */
coord_t *
str2coord(char *str, int size)
{
	if (!strcasecmp(str, "pass")) {
		return coord_pass();
	} else if (!strcasecmp(str, "resign")) {
		return coord_resign();
	} else {
		char xc = tolower(str[0]);
		return coord_init(xc - 'a' - (xc > 'i') + 1, atoi(str + 1), size);
	}
}


int
coord_edge_distance(coord_t c, struct board *b)
{
	int x = coord_x(c, b), y = coord_y(c, b);
	int dx = x > board_size(b) / 2 ? board_size(b) - 1 - x : x;
	int dy = y > board_size(b) / 2 ? board_size(b) - 1 - y : y;
	return (dx < dy ? dx : dy) - 1 /* S_OFFBOARD */;
}

int
coord_gridcular_distance(coord_t c1, coord_t c2, struct board *b)
{
	/* Gridcular metric has nice property that it makes
	 * circle-like structures on the square grid. */
	int x1 = coord_x(c1, b), y1 = coord_y(c1, b);
	int x2 = coord_x(c2, b), y2 = coord_y(c2, b);
	int dx = abs(x1 - x2), dy = abs(y1 - y2);
	return dx + dy + (dx > dy ? dx : dy);
}
