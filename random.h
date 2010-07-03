#ifndef ZZGO_RANDOM_H
#define ZZGO_RANDOM_H

#include <stdint.h>

void fast_srandom(unsigned long seed);
unsigned long fast_getseed(void);

/* Note that only 16bit numbers can be returned. */
uint16_t fast_random(unsigned int max);
/* Use this one if you want larger numbers. */
static uint32_t fast_irandom(unsigned int max);

/* Get random number in [0..1] range. */
float fast_frandom();


static inline uint32_t
fast_irandom(unsigned int max)
{
	if (max <= 65536)
		return fast_random(max);
	return (fast_random(max / 65536 + 1) << 16) | fast_random(65536);
}

#endif
