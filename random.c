/* Multi-thread random number generator */

#include <stdio.h>

#include "random.h"


/********************************************************************************************/
/* Multi-thread storage (Windows) */

/* Use TlsGetValue() / TlsSetValue() for thread-local storage,
 * mingw-w64's __thread is painfully slow. */

#ifdef _WIN32

int random_tls_index = -1;

static void __attribute__((constructor))
fast_random_init()
{
	/* Temp initial state until fast_srandom() gets called. */
	static uint64_t initial_state;

	random_tls_index = TlsAlloc();
	fast_srandom(&initial_state, 0xcafef00dd15ea5e5ULL);
}

#else

/********************************************************************************************/
/* Multi-thread storage (default) */

/* Use __thread for thread-local storage (preferred method). */

#ifndef NO_THREAD_LOCAL

__thread uint64_t pcg32_state = 0xcafef00dd15ea5e5ULL;

#else

/********************************************************************************************/
/* Multi-thread storage (pthread) */

/* No support for __thread local storage, use pthread_getspecific() instead. */

pthread_key_t random_state_key;

static void __attribute__((constructor))
fast_random_init(void)
{
	/* Temp initial state until fast_srandom() gets called. */
	static uint64_t initial_state;
	
	pthread_key_create(&random_state_key, NULL);
	fast_srandom(&initial_state, 0xcafef00dd15ea5e5ULL);
}


#endif  // NO_THREAD_LOCAL
#endif  // _WIN32


/********************************************************************************************/
/* RNG seed operations */

void
fast_srandom(uint64_t *state, uint64_t seed)
{
	if (state)  pcg32_set_state(state);
	state = pcg32_get_state();
	*state = seed;
}

uint64_t
fast_getseed(void)
{
	uint64_t *ps = pcg32_get_state();
	return *ps;
}
