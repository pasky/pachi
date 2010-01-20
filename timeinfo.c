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

void
time_add(struct timespec *when, struct timespec *len)
{
	when->tv_sec += len->tv_sec;
	when->tv_nsec += len->tv_nsec;
	if (when->tv_nsec > 1000000000)
		when->tv_sec++, when->tv_nsec -= 1000000000;
}

bool
time_passed(struct timespec *when)
{
	struct timespec now; clock_gettime(CLOCK_REALTIME, &now);

	return now.tv_sec > when->tv_sec || (now.tv_sec == when->tv_sec && now.tv_nsec > when->tv_nsec);
}
