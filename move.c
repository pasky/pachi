#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "move.h"


/* The S_OFFBOARD margin is not addressable by coordinates. */

static char asdf[] = "abcdefghjklmnopqrstuvwxyz";

char *
coord2bstr(char *buf, coord_t c, struct board *board)
{
	if (is_pass(c)) {
		return "pass";
	} else if (is_resign(c)) {
		return "resign";
	} else {
		/* Some GTP servers are broken and won't grok lowercase coords */
		snprintf(buf, 4, "%c%d", toupper(asdf[coord_x(c, board) - 1]), coord_y(c, board));
		return buf;
	}
}

/* Return coordinate in dynamically allocated buffer. */
char *
coord2str(coord_t c, struct board *board)
{
	char buf[256];
	return strdup(coord2bstr(buf, c, board));
}

/* Return coordinate in statically allocated buffer, with some backlog for
 * multiple independent invocations. Useful for debugging. */
char *
coord2sstr(coord_t c, struct board *board)
{
	static char *b;
	static char bl[10][4];
	static int bi;
	b = bl[bi]; bi = (bi + 1) % 10;
	return coord2bstr(b, c, board);
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
