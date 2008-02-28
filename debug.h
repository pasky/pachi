#ifndef ZZGO_DEBUG_H
#define ZZGO_DEBUG_H

#ifdef DEBUG
#define DEBUGL_(l, n) (unlikely((l) > (n)))
#else
#define DEBUGL_(l, n) (false)
#endif

extern int debug_level;

#define DEBUGL(n) DEBUGL_(debug_level, n)

#endif
