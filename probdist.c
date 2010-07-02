#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

//#define DEBUG
#include "debug.h"
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
	if (DEBUGL(6))
		fprintf(stderr, "stab %f / %f\n", stab, total);

	int r = 0;
	while (stab > pd->rowtotals[r] + PROBDIST_EPSILON) {
		stab -= pd->rowtotals[r];
		r++;
		assert(r < board_size(pd->b));
	}
	for (coord_t c = r * board_size(pd->b); c < board_size2(pd->b); c++) {
		if (DEBUGL(6))
			fprintf(stderr, "[%s] %f (%f)\n", coord2sstr(c, pd->b), pd->items[c], stab);
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
