#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "move.h"


static char asdf[] = "abcdefghjklmnopqrstuvwxyz";

char *
coord2str(coord_t c)
{
	char b[4];
	if (is_pass(c)) {
		return strdup("pass");
	} else if (is_resign(c)) {
		return strdup("resign");
	} else {
		/* Some GTP servers are broken and won't grok lowercase coords */
		snprintf(b, 4, "%c%d", toupper(asdf[coord_x(c)]), coord_y(c) + 1);
		return strdup(b);
	}
}

/* No sanity checking */
coord_t *
str2coord(char *str)
{
	if (!strcasecmp(str, "pass")) {
		return coord_pass();
	} else if (!strcasecmp(str, "resign")) {
		return coord_resign();
	} else {
		char xc = tolower(str[0]);
		return coord_init(xc - 'a' - (xc > 'i'), atoi(str + 1) - 1);
	}
}
