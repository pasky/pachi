#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "random/random.h"

static struct coord *
random_genmove(struct board *b, enum stone color)
{
	struct move m;
	m.color = color;

	if (board_no_valid_moves(b, color))
		return coord_pass();

	do {
		m.coord.x = random() % b->size;
		m.coord.y = random() % b->size;
	} while (!board_valid_move(b, &m, true));

	return coord_copy(m.coord);
}

struct engine *
engine_random_init(void)
{
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "RandomMove Engine";
	e->comment = "I just make random moves. I won't pass as long as there is a place on the board where I can play. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = random_genmove;
	return e;
}
