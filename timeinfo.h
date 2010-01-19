#ifndef ZZGO_TIMEINFO_H
#define ZZGO_TIMEINFO_H

/* Time-keeping information about time to spend on the next move and/or
 * rest of the game. */

/* Note that some ways of specifying time (TD_GAMES) may not make sense
 * with all engines. */

#include <stdbool.h>
#include <time.h>

struct time_info {
	/* For how long we can spend the time? */
	enum time_period {
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
		struct timespec walltime; // TD_WALLTIME
	} len;
};

/* Parse time information provided in custom format:
 *   =NUM - fixed number of simulations per move
 *   NUM - number of seconds to spend per move
 *   _NUM - number of seconds to spend per game
 *
 * Returns false on parse error.  */
bool time_parse(struct time_info *ti, char *s);

#endif
