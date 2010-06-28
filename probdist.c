#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "move.h"
#include "probdist.h"
#include "random.h"
#include "board.h"

coord_t
probdist_pick(struct probdist *pd, coord_t *ignore)
{
	double total = probdist_total(pd) - PROBDIST_EPSILON;
	assert(total >= 0);
	double stab = fast_frandom() * total;
	//fprintf(stderr, "stab %f / %f\n", stab, total);

	int r = 0;
	while (stab > pd->rowtotals[r] + PROBDIST_EPSILON) {
		stab -= pd->rowtotals[r];
		r++;
		assert(r < pd->n1);
	}
	for (coord_t c = r * pd->n1; c < pd->n; c++) {
		//struct board b = { .size = 11 };
		//fprintf(stderr, "[%s] %f (%f)\n", coord2sstr(c, &b), pd->items[c], stab);
		if (c == *ignore) {
			ignore++;
			continue;
		}
		if (stab <= pd->items[c])
			return c;
		stab -= pd->items[c];
	}

	fprintf(stderr, "overstab %f (total %f)\n", stab, total);
	assert(0);
	return -1;
}
