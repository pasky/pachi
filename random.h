#ifndef PACHI_RANDOM_H
#define PACHI_RANDOM_H

/* Multi-thread random number generator */

#include <stdint.h>
#include "util.h"

/* Return random number in [0, max-1 ] range. */
static uint32_t fast_random(unsigned int max);

/* Set random seed and provide storage for random number generator state.
 * @state: optional, set new random number state to use. If given must remain
 * valid during subsequent fast_random() calls, or until replaced by next
 * fast_srandom(). May or may not be used depending on platform. */
void     fast_srandom(uint64_t *state, uint64_t seed);

/* Get current random seed. */
uint64_t fast_getseed(void);

#if (__SIZEOF_LONG__ == 4)
#define PRIrandom_seed  "llu"
#else
#define PRIrandom_seed  "lu"
#endif


/*********************************************************************************/
/* Multi-thread storage (Windows) */

/* Use TlsGetValue() / TlsSetValue() for thread-local storage,
 * mingw-w64's __thread is painfully slow. */

#ifdef _WIN32
#include <windows.h>

extern int random_tls_index;
#define pcg32_get_state()	((uint64_t *)TlsGetValue(random_tls_index))
#define pcg32_set_state(pt)	TlsSetValue(random_tls_index, (void*)(pt))

#else

/*********************************************************************************/
/* Multi-thread storage (default) */

/* Use __thread for thread-local storage (preferred method). */

#ifndef NO_THREAD_LOCAL
extern __thread uint64_t pcg32_state;
#define pcg32_get_state()	(&pcg32_state)
#define pcg32_set_state(pt)	do { } while(0)

#else

/*********************************************************************************/
/* Multi-thread storage (pthread) */

/* __thread local storage not supported, use pthread_getspecific() instead. */

#include <pthread.h>

extern pthread_key_t random_state_key;
#define pcg32_get_state()	((uint64_t *)pthread_getspecific(random_state_key))
#define pcg32_set_state(pt)	pthread_setspecific(random_state_key, (pt))

#endif // NO_THREAD_LOCAL
#endif // _WIN32


/*********************************************************************************/
/* PCG32 fast and statistically robust random number generator by Melissa E. O'Neill
 * (c) 2014 M.E. O'Neill - Apache License 2.0
 *   https://www.pcg-random.org
 */

static inline uint32_t
random_pcg32(void)
{
	uint64_t *ps = pcg32_get_state();
	uint64_t state = *ps;
	*ps = state = state * 6364136223846793005ULL + 1442695040888963407ULL;
	uint32_t x = (uint32_t)(((state >> 18) ^ state) >> 27);
	int rot = (int)(state >> 59);
	return (rot == 0 ? x : ((x >> rot) | (x << (32-rot))));
}

/* Fast bounded RNG method: int multiplication (fast, biased)
 * The ranges we use are so small that bias shouldn't matter here.
 * See Melissa's post on bounded random number methods for details:
 *   https://www.pcg-random.org/posts/bounded-rands.html   */
static inline uint32_t
fast_random(uint32_t range) {
	uint32_t x = random_pcg32();
	uint64_t m = (uint64_t)x * (uint64_t)range;
	return m >> 32;
}


#endif
