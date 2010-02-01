#ifndef ZZGO_TIMEINFO_H
#define ZZGO_TIMEINFO_H

/* Time-keeping information about time to spend on the next move and/or
 * rest of the game. */

/* Note that some ways of specifying time (TD_GAMES) may not make sense
 * with all engines. */

#include <stdbool.h>

struct time_info {
	/* For how long we can spend the time? */
	enum time_period {
		TT_NULL, // No time limit. Other structure elements are undef.
		TT_MOVE, // Time for the next move.
		TT_TOTAL, // Time for the rest of the game.
	} period;
	/* How are we counting the time? */
	enum time_dimension {
		TD_GAMES, // Fixed number of simulations to perform.
		TD_WALLTIME, // Wall time to spend performing simulations.
	} dim;
	union {
		int games; // TD_GAMES
		struct {   // TD_WALLTIME
			/* Recommended wall time for next move or game (seconds). Does not
			 * include net lag. Play asap if 0. */
			double recommended_time;
		} t;
	} len;
};

/* Parse time information provided in custom format:
 *   =NUM - fixed number of simulations per move
 *   NUM - number of seconds to spend per move (can be float)
 *   _NUM - number of seconds to spend per game
 *
 * Returns false on parse error.  */
bool time_parse(struct time_info *ti, char *s);

/* Returns the current time. */
double time_now(void);

/* Sleep for a given interval (in seconds). Return immediately if interval < 0. */
void time_sleep(double interval);

#endif
