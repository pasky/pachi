#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define DEBUG

#include "debug.h"
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
			ti->len.t.recommended_time = atof(s);
			break;
	}
	return true;
}

/* Set correct time information before making a move, and
 * always make it time per move for the engine. */
void
time_prepare_move(struct time_info *ti, struct board *board)
{
	int moves_left;

	if (ti->period == TT_TOTAL) {
		moves_left = board_estimated_moves_left(board);
		assert(moves_left > 0);
		if (ti->dim == TD_GAMES) {
			ti->period = TT_MOVE;
			ti->len.games /= moves_left;
		}
	}
	if (ti->period == TT_NULL || ti->dim != TD_WALLTIME)
		return;

	double now = time_now();
	if (!ti->len.t.timer_start) {
		ti->len.t.timer_start = now; // we're playing the first game move
	}
	if (ti->period == TT_TOTAL) {
		ti->period = TT_MOVE;
		ti->len.t.recommended_time /= moves_left;
	}
}

/* Start our timer. kgs does this (correctly) on "play" not "genmove"
 * unless we are making the first move of the game. */
void
time_start_timer(struct time_info *ti)
{
	if (ti->period != TT_NULL && ti->dim == TD_WALLTIME)
		ti->len.t.timer_start = time_now();
}

/* Returns the current time. */
double
time_now(void)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	return now.tv_sec + now.tv_nsec/1000000000.0;
}

/* Sleep for a given interval (in seconds). Return immediately if interval < 0. */
void
time_sleep(double interval)
{
	struct timespec ts;
	double sec;
	ts.tv_nsec = (int)(modf(interval, &sec)*1000000000.0);
        ts.tv_sec = (int)sec;
	nanosleep(&ts, NULL); /* ignore error if interval was < 0 */
}
