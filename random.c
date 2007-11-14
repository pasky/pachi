#include <stdio.h>

#include "random.h"


/* Simple Park-Miller */

static unsigned long seed = 29264;

void
fast_srandom(unsigned long seed_)
{
	seed = seed_;
}

unsigned long
fast_random(unsigned long max)
{
	unsigned long hi, lo;
	lo = 16807 * (seed & 0xffff);
	hi = 16807 * (seed >> 16);
	lo += (hi & 0x7fff) << 16;
	lo += hi >> 15;
	seed = (lo & 0x7fffffff) + (lo >> 31);
	return ((seed & 0xffff) * max) >> 16;
}
