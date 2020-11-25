#ifndef PACHI_TIMEINFO_H
#define PACHI_TIMEINFO_H

/* Time-keeping information about time to spend on the next move and/or
 * rest of the game. This is only a hint, an engine may decide to spend
 * more or less time on a given move, provided it never forfeits on time. */

/* Note that some ways of specifying time (TD_GAMES) may not make sense
 * with all engines. */

#include <stdbool.h>
#include <limits.h>

#include "board.h"

enum time_type {
	TT_NULL,	// No time limit. Other structure elements are undef.
	TT_MOVE,	// Time for the next move.
	TT_TOTAL,	// Time for the rest of the game. Never seen by engine.
};

enum time_dim {
	TD_GAMES,	// Fixed number of simulations to perform.
	TD_WALLTIME,	// Wall time to spend performing simulations.
};

typedef struct {
	enum time_type type;	/* Type of limit: Per move / Game / No limit */
	enum time_dim  dim;	/* Dimension: Fixed playouts or walltime  */

	/**************************************************************************************/
	/* Fixed playouts:	(TD_GAMES) */
	
	int games;
	int games_max;

	
	/**************************************************************************************/
	/* Walltime:		(TD_WALLTIME) */
	
	double main_time;	/* Main thinking time.
				   0 if we are already completely in byoyomi. */
	
	/* Byoyomi time. This time must be remembered to avoid rushing at the end of the main
	 * period. If no byoyomi, set to 0. Otherwise, both periods and stones are larger than
	 * zero, and initially we have _periods periods of length _time and have to play _stones
	 * stones in each. If we play in canadian byoyomi, _time will shrink until we play all
	 * stones of the current period; _max always keeps period length for reference.
	 * (In normal time settings, one of _periods or _stones is 1.) */
	double byoyomi_time;
	int    byoyomi_periods;
	int    byoyomi_stones;
	double byoyomi_time_max;
	int    byoyomi_stones_max;
	bool   canadian;	// time_left field meaning changes
	
	double timer_start;	/* Absolute time at which our timer started for current move,
				 * 0 if not yet known. The engine always sees > 0. */

	/*********************************************************************************************/
	/* Flags */
	
	bool ignore_gtp;	/* If true, this time info is independent from GTP time_left updates,
				 * which will be ignored. This is the case if the time settings were
				 * forced on the command line. */
	bool can_stop_early;	/* Stop early once best move is decided (saves time / cpu).
				 * default: true for TD_WALLTIME, false for TD_GAMES */
} time_info_t;

extern const time_info_t ti_none;

// XXX ugly
static inline time_info_t ti_unlimited();


/* Parse time information provided in custom format:
 *   =NUM - fixed number of simulations per move
 *   NUM - number of seconds to spend per move (can be floating_t)
 *   _NUM - number of seconds to spend per game
 *
 * Returns false on parse error.  */
bool time_parse(time_info_t *ti, char *s);

/* Update time settings according to gtp time_settings command.
 * main_time < 0 implies no time limit. */
void time_settings(time_info_t *ti, int main_time, int byoyomi_time, int byoyomi_stones, int byoyomi_periods);

/* Update time information according to gtp time_left command. */
void time_left(time_info_t *ti, int time_left, int stones_left);

/* Start our timer. kgs does this (correctly) on "play" not "genmove"
 * unless we are making the first move of the game. */
void time_start_timer(time_info_t *ti);

/* Subtract given amount of elapsed time from time settings. */
void time_sub(time_info_t *ti, double interval, bool new_move);

/* Returns the current time. */
double time_now(void);

/* Get current time string, format like "Mar 15 07:39:50"
 * Returns static buffer */
char *time_str();

/* Sleep for a given interval (in seconds). Return immediately if interval < 0. */
void time_sleep(double interval);


/* Based on existing time information, compute the optimal/maximal time
 * to be spent on this move. */
/* The values can be negative, indicating severe time shortage (less time
 * available than netlag safety margin) and consequently need to choose
 * a move ASAP. */

typedef struct {
	/* spend this amount of time if possible */
	union {
		double time; // TD_WALLTIME
		int playouts; // TD_GAMES
	} desired;
	/* spend no more than this time */
	union {
		double time; // TD_WALLTIME
		int playouts; // TD_GAMES
	} worst;
} time_stop_t;

/* fuseki_end and yose_start are percentages of expected game length. */
void time_stop_conditions(time_info_t *ti, board_t *b, int fuseki_end, int yose_start,
			  floating_t max_maintime_ratio, time_stop_t *stop);

/* Time settings to use during fuseki */
extern time_info_t ti_fuseki;

/* time_info to use for genmove() */
time_info_t *time_info_genmove(board_t *b, time_info_t *ti, enum stone color);

/* set number of moves for fuseki for --fuseki-time */
void set_fuseki_moves(int moves);


static inline time_info_t ti_unlimited()
{
	time_info_t ti = { TT_MOVE, };
	ti.dim = TD_GAMES;
	ti.games = INT_MAX;
	ti.games_max = 0;
	return ti;
}

#endif
