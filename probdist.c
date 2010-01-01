#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "move.h"
#include "probdist.h"
#include "random.h"

struct probdist *
probdist_init(struct probdist *pd, int n)
{
	if (!pd) pd = malloc(sizeof(*pd));
	pd->n = n;
	pd->items = calloc(n, sizeof(pd->items[0]));
	pd->total = 0;
	return pd;
}

coord_t
probdist_pick(struct probdist *pd)
{
	assert(pd->total > -1); // -1..0 is rounding error
	if (pd->total < __FLT_EPSILON__)
		return pass;
	float stab = (float) fast_random(65536) / 65536 * pd->total;
	float sum = 0;
	//fprintf(stderr, "stab %f / %f\n", stab, pd->total);
	for (int i = 0; i < pd->n; i++) {
		sum += pd->items[i];
		if (stab < sum)
			return i;
	}
	//fprintf(stderr, "overstab %f (total %f, sum %f)\n", stab, pd->total, sum);
	// This can sometimes happen when also punching due to rounding errors,.
	// Just assert that the difference is tiny.
	assert(fabs(pd->total - stab) < 1);
	return pass;
}

void
probdist_done(struct probdist *pd) {
	free(pd->items);
}
