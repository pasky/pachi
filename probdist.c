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
probdist_pick(struct probdist *restrict pd, coord_t *restrict ignore)
{
	fixp_t total = probdist_total(pd);
	fixp_t stab = fast_irandom(total);
	if (DEBUGL(6))
		fprintf(stderr, "stab %f / %f\n", fixp_to_double(stab), fixp_to_double(total));

	int r = 1;
	coord_t c = board_size(pd->b) + 1;
	while (stab > pd->rowtotals[r]) {
		if (DEBUGL(6))
			fprintf(stderr, "[%s] skipping row %f (%f)\n", coord2sstr(c, pd->b), fixp_to_double(pd->rowtotals[r]), fixp_to_double(stab));

		stab -= pd->rowtotals[r];
		r++; assert(r < board_size(pd->b));

		c += board_size(pd->b);
		while (!is_pass(*ignore) && *ignore <= c)
			ignore++;
	}

	for (; c < board_size2(pd->b); c++) {
		if (DEBUGL(6))
			fprintf(stderr, "[%s] %f (%f)\n", coord2sstr(c, pd->b), fixp_to_double(pd->items[c]), fixp_to_double(stab));

		assert(is_pass(*ignore) || c <= *ignore);
		if (c == *ignore) {
			if (DEBUGL(6))
				fprintf(stderr, "\tignored\n");
			ignore++;
			continue;
		}

		if (stab <= pd->items[c])
			return c;
		stab -= pd->items[c];
	}

	fprintf(stderr, "overstab %f (total %f)\n", fixp_to_double(stab), fixp_to_double(total));
	assert(0);
	return -1;
}
