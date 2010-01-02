#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "move.h"
#include "probdist.h"
#include "random.h"

int
probdist_pick(struct probdist *pd)
{
	assert(probdist_total(pd) >= 0);
	/* TODO: float random */
	float stab = (float) fast_random(65536) / 65536 * probdist_total(pd);
	//fprintf(stderr, "stab %f / %f\n", stab, pd->items[pd->n - 1]);
	for (int i = 0; i < pd->n; i++) {
		if (stab <= pd->items[i])
			return i;
	}
	//fprintf(stderr, "overstab %f (total %f, sum %f)\n", stab, pd->items[pd->n - 1], sum);
	assert(0);
	return -1;
}
