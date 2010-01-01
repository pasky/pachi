#ifndef ZZGO_PROBDIST_H
#define ZZGO_PROBDIST_H

/* Tools for picking an item according to a probability distribution. */

#include "move.h"

struct probdist {
	int n;
	float *items; // [n]
	float total;
};

struct probdist *probdist_init(struct probdist *pd, int n);
static void probdist_add(struct probdist *pd, int i, float val);
static void probdist_mul(struct probdist *pd, int i, float val);
int probdist_pick(struct probdist *pd);
void probdist_done(struct probdist *pd); // Doesn't free pd itself


/* We disable the assertions here since this is quite time-critical
 * part of code, and also the compiler is reluctant to inline the
 * functions otherwise. */

static inline void
probdist_add(struct probdist *pd, int i, float val)
{
#if 0
	assert(i >= 0 && i < pd->n);
	assert(val >= 0);
#endif
	pd->items[i] += val;
	pd->total += val;
}

static inline void
probdist_mul(struct probdist *pd, int i, float val)
{
#if 0
	assert(i >= 0 && i < pd->n);
	assert(val >= 0);
#endif
	pd->total += (val - 1) * pd->items[i];
	pd->items[i] *= val;
}

#endif
