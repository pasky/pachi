#include <stdio.h>

#include "random.h"


/* Simple Park-Miller for floating point; LCG as used in glibc and other places */


/********************************************************************************************/
#ifdef _WIN32

/* Use TlsGetValue() / TlsSetValue() for thread-local storage,
 * mingw-w64's __thread is painfully slow. */

static int tls_index = -1;

static void __attribute__((constructor))
init_fast_random()
{
	tls_index = TlsAlloc();
	fast_srandom(29264);
}

void
fast_srandom(unsigned long seed_)
{
	TlsSetValue(tls_index, (void*)(intptr_t)seed_);
}

unsigned long
fast_getseed(void)
{
	return (unsigned long)(intptr_t)TlsGetValue(tls_index);
}

uint16_t
fast_random(unsigned int max)
{
	unsigned long pmseed = fast_getseed();
	pmseed = ((pmseed * 1103515245) + 12345) & 0x7fffffff;
	fast_srandom(pmseed);
	return ((pmseed & 0xffff) * max) >> 16;
}


#else


/********************************************************************************************/
#ifndef NO_THREAD_LOCAL

static __thread unsigned long pmseed = 29264;

void
fast_srandom(unsigned long seed_)
{
	pmseed = seed_;
}

unsigned long
fast_getseed(void)
{
	return pmseed;
}

uint16_t
fast_random(unsigned int max)
{
	pmseed = ((pmseed * 1103515245) + 12345) & 0x7fffffff;
	return ((pmseed & 0xffff) * max) >> 16;
}

float
fast_frandom(void)
{
	/* Construct (1,2) IEEE floating_t from our random integer */
	/* http://rgba.org/articles/sfrand/sfrand.htm */
	union { unsigned long ul; floating_t f; } p;
	p.ul = (((pmseed *= 16807) & 0x007fffff) - 1) | 0x3f800000;
	return p.f - 1.0f;
}

#else


/********************************************************************************************/

/* Thread local storage not supported through __thread,
 * use pthread_getspecific() instead. */

#include <pthread.h>

static pthread_key_t seed_key;

static void __attribute__((constructor))
random_init(void)
{
	pthread_key_create(&seed_key, NULL);
	fast_srandom(29264UL);
}

void
fast_srandom(unsigned long seed_)
{
	pthread_setspecific(seed_key, (void *)seed_);
}

unsigned long
fast_getseed(void)
{
	return (unsigned long)pthread_getspecific(seed_key);
}

uint16_t
fast_random(unsigned int max)
{
	unsigned long pmseed = (unsigned long)pthread_getspecific(seed_key);
	pmseed = ((pmseed * 1103515245) + 12345) & 0x7fffffff;
	pthread_setspecific(seed_key, (void *)pmseed);
	return ((pmseed & 0xffff) * max) >> 16;
}

float
fast_frandom(void)
{
	/* Construct (1,2) IEEE floating_t from our random integer */
	/* http://rgba.org/articles/sfrand/sfrand.htm */
	unsigned long pmseed = (unsigned long)pthread_getspecific(seed_key);
	pmseed *= 16807;
	union { unsigned long ul; floating_t f; } p;
	p.ul = ((pmseed & 0x007fffff) - 1) | 0x3f800000;
	pthread_setspecific(seed_key, (void *)pmseed);
	return p.f - 1.0f;
}

#endif
#endif
