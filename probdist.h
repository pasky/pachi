#ifndef ZZGO_PROBDIST_H
#define ZZGO_PROBDIST_H

/* Tools for picking an item according to a probability distribution. */

/* The probability distribution structure is designed to be once
 * initialized, then each item sequentially assigned a value one,
 * then multiple times an item picked randomly. */

#include "move.h"

struct probdist {
	int n;
	float *items; // [n], probability pick<=i
};

struct probdist *probdist_init(struct probdist *pd, int n);
/* You must call this for all items, *in sequence* (0, 1, ...).
 * @val is probability of item @i (as opposed to items[i], which
 * is probability of item <=i, thus includes the sum of predecessors
 * as well). */
static void probdist_set(struct probdist *pd, int i, float val);
int probdist_pick(struct probdist *pd);
void probdist_done(struct probdist *pd); // Doesn't free pd itself


/* We disable the assertions here since this is quite time-critical
 * part of code, and also the compiler is reluctant to inline the
 * functions otherwise. */

static inline void
probdist_set(struct probdist *pd, int i, float val)
{
#if 0
	assert(i >= 0 && i < pd->n);
	assert(val >= 0);
#endif
	pd->items[i] = (__builtin_expect(i > 0, 1) ? pd->items[i - 1] : 0)
	               + val;
}

#endif
