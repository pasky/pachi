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
	float total = probdist_total(pd) - PROBDIST_EPSILON;
	assert(total >= 0);
	/* TODO: float random */
	double stab = fast_frandom() * total;
	//fprintf(stderr, "stab %f / %f\n", stab, total);
	for (int i = 0; i < pd->n; i++) {
		if (stab <= pd->items[i])
			return i;
		stab -= pd->items[i];
	}
	fprintf(stderr, "overstab %f (total %f)\n", stab, total);
	assert(0);
	return -1;
}
