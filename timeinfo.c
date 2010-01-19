#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "timeinfo.h"


bool
time_parse(struct time_info *ti, char *s)
{
	switch (s[0]) {
		case '_': ti->period = TT_TOTAL; s++; break;
		default: ti->period = TT_MOVE; break;
	}
	switch (s[0]) {
		case '=':
			ti->dim = TD_GAMES;
			ti->len.games = atoi(++s);
			break;
		default:
			if (!isdigit(s[0]))
				return false;
			ti->dim = TD_WALLTIME;
			ti->len.walltime.tv_sec = atoi(s);
			ti->len.walltime.tv_nsec = 0;
			break;
	}
	return true;
}
