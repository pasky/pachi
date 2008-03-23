#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "montecarlo/internal.h"
#include "playout/moggy.h"
#include "random.h"


coord_t
playout_moggy(struct montecarlo *mc, struct board *b, enum stone our_real_color)
{
	/* Local checks */

	if (!is_pass(b->last_move.coord)) {
		// TODO
	}

	/* Global checks */

	/* Any groups in atari? */
	if (b->clen > 0) {
		group_t group = b->c[fast_random(b->clen)];
		enum stone color = board_at(b, group);
		coord_t lib = board_group_info(b, group).lib[0];

		/* Do not suicide. */
		if (valid_escape_route(b, color, lib))
			return lib;
	}

	return pass;
}
