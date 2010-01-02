#include <stdio.h>

#include "random.h"


/* Simple Park-Miller */

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

unsigned long
fast_random(unsigned int max)
{
	unsigned long hi, lo;
	lo = 16807 * (pmseed & 0xffff);
	hi = 16807 * (pmseed >> 16);
	lo += (hi & 0x7fff) << 16;
	lo += hi >> 15;
	pmseed = (lo & 0x7fffffff) + (lo >> 31);
	return ((pmseed & 0xffff) * max) >> 16;
}

float
fast_frandom(void)
{
	/* Construct (1,2) IEEE float from our random integer */
	/* http://rgba.org/articles/sfrand/sfrand.htm */
	union { unsigned long ul; float f; } p;
	p.ul = (((pmseed *= 16807) & 0x007fffff) - 1) | 0x3f800000;
	return p.f - 1.0f;
}
