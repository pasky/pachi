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

/* For safety, use at most 3 times the desired time on a single move
 * in main time, and 1.1 times in byoyomi. */
#define MAX_MAIN_TIME_EXTENSION 3.0
#define MAX_BYOYOMI_TIME_EXTENSION 1.1

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
	if (stones_left == 0) {
		/* Main time */
		ti->period = TT_TOTAL;
		ti->len.t.recommended_time = ti->len.t.max_time;
		/* byoyomi_time unchanged. */
	} else {
		ti->period = TT_MOVE;
		ti->len.t.byoyomi_time = ((double)time_left)/stones_left;
		ti->len.t.recommended_time = ti->len.t.byoyomi_time;
	}
}

/* Returns true if we are in byoyomi (or should play as if in byo yomi
 * because remaining time per move in main time is less than byoyomi time
 * per move). */
bool
time_in_byoyomi(struct time_info *ti) {
	return ti->period == TT_MOVE && ti->dim == TD_WALLTIME && ti->len.t.byoyomi_time > 0
	       && ti->len.t.recommended_time <= ti->len.t.byoyomi_time + 0.001;
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


/* Pre-process time_info for search control and sets the desired stopping conditions. */
void
time_stop_conditions(struct time_info *ti, struct board *b, int fuseki_end, int yose_start, struct time_stop *stop)
{
	/* We must have _some_ limits by now, be it random default values! */
	assert(ti->period != TT_NULL);

	/* Minimum net lag (seconds) to be reserved in the time for move. */
	double net_lag = MAX_NET_LAG;
	/* Estimated number moves for us to make yet. */
	int moves_left = board_estimated_moves_left(b);
	assert(moves_left > 0);

	/* Special-case limit by number of simulations. */
	if (ti->dim == TD_GAMES) {
		if (ti->period == TT_TOTAL) {
			ti->period = TT_MOVE;
			ti->len.games /= moves_left;
		}

		stop->desired.playouts = ti->len.games;
		/* We force worst == desired, so note that we will NOT loop
		 * until best == winner. */
		stop->worst.playouts = ti->len.games;
		return;
	}

	assert(ti->dim == TD_WALLTIME);

	/*** Transform @ti to TT_MOVE and set up recommended/max time and
	 * net lag information. */

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
		if (DEBUGL(1) && lag > net_lag)
			fprintf(stderr, "measured lag %0.2f > computed net_lag %0.2f\n", lag, net_lag);
	}
	if (ti->period == TT_TOTAL) {
		if (ti->len.t.byoyomi_time > 0) {
			/* For non-canadian byoyomi with N>1 periods, we use N-1 periods as main time,
			 * keeping the last one as insurance against unexpected net lag. */
			if (ti->len.t.byoyomi_periods > 2) {
				ti->len.t.max_time += (ti->len.t.byoyomi_periods - 2) * ti->len.t.byoyomi_time;
				// Will add 1 more byoyomi_time just below
			}
			ti->len.t.max_time += ti->len.t.byoyomi_time;
			ti->len.t.recommended_time = ti->len.t.max_time;

			/* Maximize the number of moves played uniformly in main time, while
			 * not playing faster in main time than in byoyomi. At this point,
			 * the main time remaining is ti->len.t.max_time and already includes
			 * the first (canadian) or N-1 byoyomi periods.
			 *    main_speed = max_time / main_moves >= byoyomi_time
                         * => main_moves <= max_time / byoyomi_time */
			double actual_byoyomi = ti->len.t.byoyomi_time - net_lag;
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
		net_lag = safe_margin;
	}

	if (DEBUGL(1))
		fprintf(stderr, "recommended_time %0.2f, max_time %0.2f, byoyomi %0.2f, lag %0.2f max %0.2f\n",
			ti->len.t.recommended_time, ti->len.t.max_time, ti->len.t.byoyomi_time, lag,
			net_lag);


	/*** Setup desired/worst time limits based on recommended/max time. */

	assert(ti->period == TT_MOVE);

	double desired_time = ti->len.t.recommended_time;
        double worst_time;
	if (time_in_byoyomi(ti)) {
		// make recommended == average(desired, worst)
		worst_time = desired_time * MAX_BYOYOMI_TIME_EXTENSION;
		desired_time *= (2 - MAX_BYOYOMI_TIME_EXTENSION);

	} else {
		int bsize = (board_size(b)-2)*(board_size(b)-2);
		fuseki_end = fuseki_end * bsize / 100; // move nb at fuseki end
		yose_start = yose_start * bsize / 100; // move nb at yose start
		assert(fuseki_end < yose_start);

		/* Before yose, spend some extra. */
		if (b->moves < yose_start) {
			int moves_to_yose = (yose_start - b->moves) / 2;
			// ^- /2 because we only consider the moves we have to play ourselves
			int left_at_yose_start = moves_left - moves_to_yose;
			if (left_at_yose_start < MIN_MOVES_LEFT)
				left_at_yose_start = MIN_MOVES_LEFT;
			double longest_time = ti->len.t.max_time / left_at_yose_start;
			if (longest_time < desired_time) {
				// Should rarely happen, but keep desired_time anyway
			} else if (b->moves < fuseki_end) {
				assert(fuseki_end > 0);
				desired_time += ((longest_time - desired_time) * b->moves) / fuseki_end;
			} else { assert(b->moves < yose_start);
				desired_time = longest_time;
			}
		}
		worst_time = desired_time * MAX_MAIN_TIME_EXTENSION;
	}
	if (worst_time > ti->len.t.max_time)
		worst_time = ti->len.t.max_time;
	if (desired_time > worst_time)
		desired_time = worst_time;

	stop->desired.time = ti->len.t.timer_start + desired_time - net_lag;
	stop->worst.time = ti->len.t.timer_start + worst_time - net_lag;
	// Both stop points may be in the past if too much lag.

	if (DEBUGL(2))
		fprintf(stderr, "desired time %.02f, worst %.02f\n", desired_time, worst_time);
}
