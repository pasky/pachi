#ifndef PACHI_DEBUG_H
#define PACHI_DEBUG_H

#include <stdbool.h>

#ifdef DEBUG
#define DEBUGL_(l, n) (unlikely((l) > (n)))
#define DEBUG_MODE (true)
#else
#define DEBUGL_(l, n) (false)
#define DEBUG_MODE (false)
#endif

extern int debug_level;
extern int saved_debug_level;
extern bool debug_boardprint;

#define DEBUGL(n) DEBUGL_(debug_level, n)

/* Temporarily turn off debugging */
#define DEBUG_QUIET()	do {				\
		saved_debug_level = debug_level;	\
		debug_level = 0;			\
	} while (0)

#define DEBUG_QUIET_END() do {				\
		debug_level = saved_debug_level;	\
	} while (0)

#define QUIET(code)	do {		\
		DEBUG_QUIET();		\
		code;			\
		DEBUG_QUIET_END();	\
	} while (0)


/* The distributed engine can be _very_ verbose so use DEBUGV
 * to keep only the first N verbose logs. */
#ifndef MAX_VERBOSE_LOGS
#  define MAX_VERBOSE_LOGS 100000
#endif
extern long verbose_logs;
#define DEBUGV(verbose, n) (DEBUGL(n) && (!(verbose) || ++verbose_logs < MAX_VERBOSE_LOGS))
#define DEBUGVV(n) DEBUGV(true, (n))

#endif
