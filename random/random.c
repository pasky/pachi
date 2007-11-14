#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "random/random.h"

static coord_t *
random_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct board b2;
	board_copy(&b2, b);

	coord_t coord;
	board_play_random(&b2, color, &coord);
	if (group_at(&b2, coord) == 0) {
		/* This was suicide. Just pass. */
		/* XXX: We should check for non-suicide alternatives. */
		return coord_pass();
	}

	board_done_noalloc(&b2);

	return coord_copy(coord);
}

struct engine *
engine_random_init(char *arg)
{
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "RandomMove Engine";
	e->comment = "I just make random moves. I won't pass as long as there is a place on the board where I can play. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = random_genmove;

	if (arg)
		fprintf(stderr, "Random: I support no engine arguments\n");

	return e;
}
