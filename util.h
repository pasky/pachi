#ifndef PACHI_UTIL_H
#define PACHI_UTIL_H

#include <stdlib.h>

/* Portability definitions. */

#ifdef _WIN32
#include <windows.h>

#define sleep(seconds) Sleep((seconds) * 1000)
#define __sync_fetch_and_add(ap, b) InterlockedExchangeAdd((LONG volatile *) (ap), (b));
#define __sync_fetch_and_sub(ap, b) InterlockedExchangeAdd((LONG volatile *) (ap), -(b));

/* MinGW gcc, no function prototype for built-in function stpcpy() */ 
char *stpcpy (char *dest, const char *src);

#include <ctype.h>
static inline const char *
strcasestr(const char *haystack, const char *needle)
{
	for (const char *p = haystack; *p; p++) {
		for (int ni = 0; needle[ni]; ni++) {
			if (!p[ni])
				return NULL;
			if (toupper(p[ni]) != toupper(needle[ni]))
				goto more_hay;
		}
		return p;
more_hay:;
	}
	return NULL;
}
#endif

/* Misc. definitions. */

/* Use make DOUBLE=1 in large configurations with counts > 1M
 * where 24 bits of floating_t mantissa become insufficient. */
#ifdef DOUBLE
#  define floating_t double
#  define PRIfloating "%lf"
#else
#  define floating_t float
#  define PRIfloating "%f"
#endif

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect((x), 0)

static inline void *
checked_malloc(size_t size, char *filename, unsigned int line, const char *func)
{
	void *p = malloc(size);
	if (!p) {
		fprintf(stderr, "%s:%u: %s: OUT OF MEMORY malloc(%zu)\n",
			filename, line, func, size);
		exit(1);
	}
	return p;
}

static inline void *
checked_calloc(size_t nmemb, size_t size, char *filename, unsigned int line, const char *func)
{
	void *p = calloc(nmemb, size);
	if (!p) {
		fprintf(stderr, "%s:%u: %s: OUT OF MEMORY calloc(%zu, %zu)\n",
			filename, line, func, nmemb, size);
		exit(1);
	}
	return p;
}

#define malloc2(size)        checked_malloc((size), __FILE__, __LINE__, __func__)
#define calloc2(nmemb, size) checked_calloc((nmemb), (size), __FILE__, __LINE__, __func__)

#endif
