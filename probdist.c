#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "move.h"
#include "probdist.h"
#include "random.h"
#include "board.h"

int
probdist_pick(struct probdist *pd, int *ignore)
{
	double total = probdist_total(pd) - PROBDIST_EPSILON;
	assert(total >= 0);
	double stab = fast_frandom() * total;
	//fprintf(stderr, "stab %f / %f\n", stab, total);
	for (int i = 0; i < pd->n; i++) {
		//struct board b = { .size = 11 };
		//fprintf(stderr, "[%s] %f (%f)\n", coord2sstr(i, &b), pd->items[i], stab);
		if (*ignore && i == *ignore) {
			ignore++;
			continue;
		}
		if (stab <= pd->items[i])
			return i;
		stab -= pd->items[i];
	}
	fprintf(stderr, "overstab %f (total %f)\n", stab, total);
	assert(0);
	return -1;
}
