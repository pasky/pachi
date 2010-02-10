#ifndef ZZGO_TIMEINFO_H
#define ZZGO_TIMEINFO_H

/* Time-keeping information about time to spend on the next move and/or
 * rest of the game. This is only a hint, an engine may decide to spend
 * more or less time on a given move, provided it never forfeits on time. */

/* Note that some ways of specifying time (TD_GAMES) may not make sense
 * with all engines. */

#include <stdbool.h>

#include "board.h"

struct time_info {
	/* For how long we can spend the time? */
	enum time_period {
		TT_NULL, // No time limit. Other structure elements are undef.
		TT_MOVE, // Time for the next move.
		TT_TOTAL, // Time for the rest of the game. Never seen by engine.
	} period;
	/* How are we counting the time? */
	enum time_dimension {
		TD_GAMES, // Fixed number of simulations to perform.
		TD_WALLTIME, // Wall time to spend performing simulations.
	} dim;
	/* The actual time count. */
	union {
		int games; // TD_GAMES
		struct {   // TD_WALLTIME
			/* Recommended wall time for next move or game (seconds).
			 * Does not include net lag. Play asap if 0. */
			double recommended_time;

			/* Maximum wall time for next move or game. Will lose on time
			 * if exceeded. Does not include net lag. Play asap if 0. */
			double max_time;

			/* Minimum net lag (seconds) to be reserved by the engine.
			 * The engine may use a larger safety margin. */
			double net_lag;

			/* Absolute time at which our timer started for current move,
			 * 0 if not yet known. The engine always sees > 0. */
			double timer_start;

			/* --- PRIVATE DATA --- */
			/* Byoyomi time per move (even for TT_TOTAL). This time must
			 * be remembered to avoid rushing at the end of the main
			 * period. 0 if no byoyomi.  An engine should only consider
			 * recommended_time, the generic time control code always sets it to
			 * the best option (play on main time or on byoyomi time). */
			double byoyomi_time;
			int byoyomi_periods; /* > 0 only for non-canadian byoyomi */
		} t;
	} len;
	/* If true, this time info is independent from GTP time_left updates,
	 * which will be ignored. This is the case if the time settings were
	 * forced on the command line. */
	bool ignore_gtp;
};

/* Parse time information provided in custom format:
 *   =NUM - fixed number of simulations per move
 *   NUM - number of seconds to spend per move (can be float)
 *   _NUM - number of seconds to spend per game
 *
 * Returns false on parse error.  */
bool time_parse(struct time_info *ti, char *s);

/* Update time settings according to gtp time_settings command. */
void time_settings(struct time_info *ti, int main_time, int byoyomi_time, int byoyomi_stones, int byoyomi_periods);

/* Update time information according to gtp time_left command. */
void time_left(struct time_info *ti, int time_left, int stones_left);

/* Returns true if we are in byoyomi (or should play as if in byo yomi
 * because remaining time per move in main time is less than byoyomi time
 * per move). */
bool time_in_byoyomi(struct time_info *ti);

/* Start our timer. kgs does this (correctly) on "play" not "genmove"
 * unless we are making the first move of the game. */
void time_start_timer(struct time_info *ti);

/* Returns the current time. */
double time_now(void);

/* Sleep for a given interval (in seconds). Return immediately if interval < 0. */
void time_sleep(double interval);


/* Based on existing time information, compute the optimal/maximal time
 * to be spent on this move. */

struct time_stop {
	/* stop at that time if possible */
	union {
		double time; // TD_WALLTIME
		int playouts; // TD_GAMES
	} desired;
	/* stop no later than this */
	union {
		double time; // TD_WALLTIME
		int playouts; // TD_GAMES
	} worst;
};

/* fuseki_end and yose_start are percentages of expected game length. */
void time_stop_conditions(struct time_info *ti, struct board *b, int fuseki_end, int yose_start, struct time_stop *stop);

#endif
