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
	int n, n1;
	double *items; // [n], items[i] = P(pick==i)
	double *rowtotals; // [n1], [i] = sum of items in row i
	double total;
};
#define probdist_total(pd) ((pd)->total)
#define probdist_one(pd, i) ((pd)->items[i])
/* Probability so small that it's same as zero; used to compensate
 * for probdist.total inaccuracies. */
#define PROBDIST_EPSILON 0.05

static void probdist_set(struct probdist *pd, int i, double val);
static void probdist_mute(struct probdist *pd, int i);

/* Pick a random item. ignore is a zero-terminated sorted array of items
 * that are not to be considered (and whose values are not in @total). */
int probdist_pick(struct probdist *pd, int *ignore);


/* We disable the assertions here since this is quite time-critical
 * part of code, and also the compiler is reluctant to inline the
 * functions otherwise. */
static inline void
probdist_set(struct probdist *pd, int i, double val)
{
#if 0
	assert(i >= 0 && i < pd->n);
	assert(val >= 0);
#endif
	pd->total += val - pd->items[i];
	pd->rowtotals[i / pd->n1] += val - pd->items[i];
	pd->items[i] = val;
}

/* Remove the item from the totals; this is used when you then
 * pass it in the ignore list to probdist_pick(). */
static inline void
probdist_mute(struct probdist *pd, int i)
{
	pd->total -= pd->items[i];
	pd->rowtotals[i / pd->n1] -= pd->items[i];
}

#endif
