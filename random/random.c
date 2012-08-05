#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "random/random.h"

static coord_t *
random_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	/* Play a random coordinate. However, we must also guard
	 * against suicide moves; repeat playing while it's a suicide
	 * unless we keep suiciding; in that case, we probably don't
	 * have any other moves available and we pass. */
	coord_t coord;
	int i = 0; bool suicide = false;

	do {
		/* board_play_random() actually plays the move too;
		 * this is desirable for MC simulations but not within
		 * the genmove. Make a scratch new board for it. */
		struct board b2;
		board_copy(&b2, b);

		board_play_random(&b2, color, &coord, NULL, NULL);

		suicide = (coord != pass && !group_at(&b2, coord));
		board_done_noalloc(&b2);
	} while (suicide && i++ < 100);

	return coord_copy(suicide ? pass : coord);
}

struct engine *
engine_random_init(char *arg, struct board *b)
{
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "RandomMove";
	e->comment = "I just make random moves. I won't pass as long as there is a place on the board where I can play. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = random_genmove;

	if (arg)
		fprintf(stderr, "Random: I support no engine arguments\n");

	return e;
}
