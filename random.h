#ifndef ZZGO_RANDOM_H
#define ZZGO_RANDOM_H

#include <stdint.h>

void fast_srandom(unsigned long seed);
unsigned long fast_getseed(void);
/* Note that only 16bit numbers can be returned. */
uint16_t fast_random(unsigned int max);
/* Get random number in [0..1] range. */
float fast_frandom();

#endif
