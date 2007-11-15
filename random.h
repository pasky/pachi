#ifndef ZZGO_RANDOM_H
#define ZZGO_RANDOM_H

void fast_srandom(unsigned long seed);
/* Note that only 16bit numbers can be returned. */
unsigned long fast_random(unsigned int max);

#endif
