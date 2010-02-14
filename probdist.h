#ifndef ZZGO_PROBDIST_H
#define ZZGO_PROBDIST_H

/* Tools for picking an item according to a probability distribution. */

/* The probability distribution structure is designed to be once
 * initialized, then random items assigned a value repeatedly and
 * random items picked repeatedly as well. */

#include "move.h"
#include "util.h"

/* The interface looks a bit funny-wrapped since we used to switch
 * between different probdist representations. */

struct probdist {
	int n;
	float *items; // [n], items[i] = P(pick==i)
	float total;
};
#define probdist_total(pd) ((pd)->total)
#define probdist_one(pd, i) ((pd)->items[i])
/* Probability so small that it's same as zero; used to compensate
 * for probdist.total inaccuracies. */
#define PROBDIST_EPSILON 0.01

static void probdist_set(struct probdist *pd, int i, float val);

int probdist_pick(struct probdist *pd);


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
	pd->total += val - pd->items[i];
	pd->items[i] = val;
}

#endif
