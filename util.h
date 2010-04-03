#ifndef ZZGO_UTIL_H
#define ZZGO_UTIL_H

#include <stdlib.h>

/* Misc. definitions. */

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect((x), 0)

static inline void *
checked_malloc(size_t size, char *filename, unsigned int line, const char *func)
{
	void *p = malloc(size);
	if (!p) {
		fprintf(stderr, "%s:%u: %s: OUT OF MEMORY malloc(%lu)\n",
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
		fprintf(stderr, "%s:%u: %s: OUT OF MEMORY calloc(%lu, %lu)\n",
			filename, line, func, nmemb, size);
		exit(1);
	}
	return p;
}

#define malloc2(size)        checked_malloc((size), __FILE__, __LINE__, __func__)
#define calloc2(nmemb, size) checked_calloc((nmemb), (size), __FILE__, __LINE__, __func__)

#endif
