#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define DEBUG

#include "debug.h"
#include "timeinfo.h"

#define MAX_NET_LAG 2.0 /* Max net lag in seconds. TODO: estimate dynamically. */
#define RESERVED_BYOYOMI_PERCENT 15 /* Reserve 15% of byoyomi time as safety margin if risk of losing on time */

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
			ti->len.t.max_time = ti->len.t.recommended_time;
			ti->len.t.net_lag = MAX_NET_LAG;
			ti->len.t.timer_start = 0;
			ti->len.t.byoyomi_time = 0.0;
			ti->len.t.byoyomi_periods = 0;
			break;
	}
	return true;
}

/* Update time settings according to gtp time_settings or kgs-time_settings command. */
void
time_settings(struct time_info *ti, int main_time, int byoyomi_time, int byoyomi_stones, int byoyomi_periods)
{
	if (byoyomi_time > 0 && byoyomi_stones == 0) {
		ti->period = TT_NULL; // no time limit, rely on engine default
	} else {
		ti->period = TT_TOTAL;
		ti->dim = TD_WALLTIME;
		ti->len.t.max_time = (double) main_time; // byoyomi will be added at next genmove
		ti->len.t.recommended_time = ti->len.t.max_time;
		ti->len.t.timer_start = 0;
		ti->len.t.net_lag = MAX_NET_LAG;
		ti->len.t.byoyomi_time = (double) byoyomi_time;
		if (byoyomi_stones > 0)
			ti->len.t.byoyomi_time /= byoyomi_stones;
		ti->len.t.byoyomi_periods = byoyomi_periods;
	}
}

/* Update time information according to gtp time_left command.
 * kgs doesn't give time_left for the first move, so make sure
 * that just time_settings + time_select_best still work. */
void
time_left(struct time_info *ti, int time_left, int stones_left)
{
	assert(ti->period != TT_NULL);
	ti->dim = TD_WALLTIME;
	ti->len.t.max_time = (double)time_left;

	if (ti->len.t.byoyomi_periods > 0 && stones_left > 0) {
		ti->len.t.byoyomi_periods = stones_left; // field misused by kgs
		stones_left = 1;
	}
	/* For non-canadian byoyomi, we use all periods as main time. */
	if (stones_left == 0 || ti->len.t.byoyomi_periods > 1) {
		/* Main time */
		ti->period = TT_TOTAL;
		ti->len.t.recommended_time = ti->len.t.max_time;
		/* byoyomi_time, net_lag & timer_start unchanged. */
	} else {
		ti->period = TT_MOVE;
		ti->len.t.byoyomi_time = ((double)time_left)/stones_left;
		ti->len.t.recommended_time = ti->len.t.byoyomi_time;
		/* net_lag & timer_start unchanged. */
	}
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
	double lag;
	if (!ti->len.t.timer_start) {
		ti->len.t.timer_start = now; // we're playing the first game move
		lag = 0;
	} else {
		lag = now - ti->len.t.timer_start;
		// TODO: keep statistics to get good estimate of lag not just current move
		ti->len.t.max_time -= lag; // can become < 0, taken into account below
		ti->len.t.recommended_time -= lag;
		if (DEBUGL(1) && lag > MAX_NET_LAG)
			fprintf(stderr, "lag %0.2f > max_net_lag %0.2f\n", lag, MAX_NET_LAG);
	}
	if (ti->period == TT_TOTAL) {
		/* For non-canadian byoyomi, we use all periods as main time, just making sure
		 * to avoid running out of the last one. */
		if (ti->len.t.byoyomi_periods > 1) {
			ti->len.t.max_time += (ti->len.t.byoyomi_periods - 1) * ti->len.t.byoyomi_time;
			// Will add 1 more byoyomi_time just below
		}
		if (ti->len.t.byoyomi_time > 0) {
			ti->len.t.max_time += ti->len.t.byoyomi_time;
			ti->len.t.recommended_time = ti->len.t.max_time;

			/* Maximize the number of moves played uniformly in main time, while
			 * not playing faster in main time than in byoyomi. At this point,
			 * the main time remaining is ti->len.t.max_time and already includes
			 * the first (canadian) or all byoyomi periods.
			 *    main_speed = max_time / main_moves >= byoyomi_time
                         * => main_moves <= max_time / byoyomi_time */
			double actual_byoyomi = ti->len.t.byoyomi_time - MAX_NET_LAG;
			if (actual_byoyomi > 0) {
				int main_moves = (int)(ti->len.t.max_time / actual_byoyomi);
				if (moves_left > main_moves)
					moves_left = main_moves; // will do the rest in byoyomi
				if (moves_left <= 0) // possible if too much lag
					moves_left = 1;
			}
		}
		ti->period = TT_MOVE;
		ti->len.t.recommended_time /= moves_left;
	}
	// To simplify the engine code, do not leave negative times:
	if (ti->len.t.recommended_time < 0)
		ti->len.t.recommended_time = 0;
	if (ti->len.t.max_time < 0)
		ti->len.t.max_time = 0;
	assert(ti->len.t.recommended_time <= ti->len.t.max_time + 0.001);

	/* Use a larger safety margin if we risk losing on time on this move: */
        double safe_margin = RESERVED_BYOYOMI_PERCENT * ti->len.t.byoyomi_time/100;
	if (safe_margin > MAX_NET_LAG && ti->len.t.recommended_time >= ti->len.t.max_time - MAX_NET_LAG) {
		ti->len.t.net_lag = safe_margin;
	} else {
		ti->len.t.net_lag = MAX_NET_LAG;
	}

	if (DEBUGL(1))
		fprintf(stderr, "recommended_time %0.2f, max_time %0.2f, byoyomi %0.2f, lag %0.2f max %0.2f\n",
			ti->len.t.recommended_time, ti->len.t.max_time, ti->len.t.byoyomi_time, lag,
			ti->len.t.net_lag);
}

/* Start our timer. kgs does this (correctly) on "play" not "genmove"
 * unless we are making the first move of the game. */
void
time_start_timer(struct time_info *ti)
{
	if (ti->period != TT_NULL && ti->dim == TD_WALLTIME)
		ti->len.t.timer_start = time_now();
}

/* Returns true if we are in byoyomi (or should play as if in byo yomi
 * because remaining time per move in main time is less than byoyomi time
 * per move). */
bool
time_in_byoyomi(struct time_info *ti) {
	return ti->period == TT_MOVE && ti->dim == TD_WALLTIME && ti->len.t.byoyomi_time > 0
	       && ti->len.t.recommended_time <= ti->len.t.byoyomi_time + 0.001;
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
