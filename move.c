#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "move.h"


/* The S_OFFBOARD margin is not addressable by coordinates. */

static char asdf[] = "abcdefghjklmnopqrstuvwxyz";

char *
coord2bstr(char *buf, coord_t c)
{
	if (is_pass(c))   return "pass";
	if (is_resign(c)) return "resign";
	
	/* Some GTP servers are broken and won't grok lowercase coords */
	snprintf(buf, 4, "%c%u", toupper(asdf[coord_x(c) - 1]), coord_y(c));
	return buf;
}

/* Return coordinate in dynamically allocated buffer. */
char *
coord2str(coord_t c)
{
	char buf[256];
	return strdup(coord2bstr(buf, c));
}

/* Return coordinate in statically allocated buffer, with some backlog for
 * multiple independent invocations. Useful for debugging. */
char *
coord2sstr(coord_t c)
{
	static char *b;
	static char bl[10][4];
	static int bi;
	b = bl[bi]; bi = (bi + 1) % 10;
	return coord2bstr(b, c);
}

/* No sanity checking */
coord_t
str2coord_for(char *str, int size)
{
	if (!strcasecmp(str, "pass"))    return pass;
	if (!strcasecmp(str, "resign"))	 return resign;
	
	char xc = tolower(str[0]);
	return xc - 'a' - (xc > 'i') + 1 + atoi(str + 1) * size;
}

coord_t
str2coord(char *str)
{
	return str2coord_for(str, the_board_size());
}

/* Must match rotations in pthashes_init() */
coord_t
rotate_coord(coord_t c, int rot)
{
	assert(c != pass);
	int size = the_real_board_size();
	int x = coord_x(c);
	int y = coord_y(c);
	
	if (rot & 1)  y = size - y + 1;
	if (rot & 2)  x = size - x + 1;
	if (rot & 4)  {  int tmp = x;  x = size - y + 1;  y = tmp;  }
	return coord_xy(x, y);
}
