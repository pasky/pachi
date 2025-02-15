#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "move.h"


/* Check @s is valid coord for given board size. */
static bool
valid_coord_for(char *s, int size)
{
	if (!s || !s[0])
		return false;

	if (!strcasecmp(s, "pass") || !strcasecmp(s, "resign"))
		return true;

	char c1 = tolower(s[0]);
	char c2 = s[1];
	int  x  = (c1 - 'a') + 1 - (c1 > 'i');
	int  y  = atoi(s + 1);
	int  digits = (y > 9 ? 2 : 1);
	char endc = (s + 1)[digits];
	assert(size <= 25);				// 'z' last letter
	
	return (c1 != 'i' && isdigit(c2) && (isspace(endc) || endc == 0) &&
		x >= 1    && x <= size   &&
		y >= 1    && y <= size);
}

/* Check @s is valid coord for current board size. */
bool
valid_coord(char *s)
{
	return valid_coord_for(s, the_board_rsize());
}


/* The S_OFFBOARD margin is not addressable by coordinates. */

static char asdf[] = "abcdefghjklmnopqrstuvwxyz";

char *
coord2bstr(char *buf, coord_t c)
{
	if (is_pass(c))   return "pass";
	if (is_resign(c)) return "resign";
	
	/* Some GTP servers are broken and won't grok lowercase coords */
	snprintf(buf, 4, "%c%i", toupper(asdf[coord_x(c) - 1]), coord_y(c) % 100);
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

/* Aborts if coord is invalid. */
coord_t
str2coord_for(char *str, int size)
{
	if (!strcasecmp(str, "pass"))    return pass;
	if (!strcasecmp(str, "resign"))	 return resign;

	assert(valid_coord_for(str, size));

	int  stride = size + 2;
	char xc = tolower(str[0]);
	int  x = (xc - 'a') + 1 - (xc > 'i');
	int  y = atoi(str + 1);
	
	return (y * stride + x);
}

coord_t
str2coord(char *str)
{
	return str2coord_for(str, the_board_rsize());
}

/* Must match rotations in pthashes_init() */
coord_t
rotate_coord(coord_t c, int rot)
{
	assert(c != pass);
	int size = the_board_rsize();
	int x = coord_x(c);
	int y = coord_y(c);
	
	if (rot & 1)  y = size - y + 1;
	if (rot & 2)  x = size - x + 1;
	if (rot & 4)  {  int tmp = x;  x = size - y + 1;  y = tmp;  }
	return coord_xy(x, y);
}
