#ifndef ZZGO_UTIL_H
#define ZZGO_UTIL_H

/* Misc. definitions. */

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#endif
