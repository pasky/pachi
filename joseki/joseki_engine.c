#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "joseki.h"
#include "joseki_engine.h"

static void
joseki_engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
			 best_moves_t *best)
{
	coord_t coords[BOARD_MAX_COORDS];
	float ratings[BOARD_MAX_COORDS];
	int matches = joseki_list_moves(joseki_dict, b, color, coords, ratings);
	
	get_joseki_best_moves(b, coords, ratings, matches, best);
	print_joseki_best_moves(best);
}

static coord_t
joseki_engine_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	coord_t best_c[20];
	float   best_r[20];
	best_moves_setup(best, best_c, best_r, 20);
	joseki_engine_best_moves(e, b, ti, color, &best);

	return best_c[0];
}

void
joseki_engine_init(engine_t *e, board_t *b)
{
	e->name = "Joseki";
	e->comment = "I select joseki moves blindly, if there are none i just pass.";
	e->genmove = joseki_engine_genmove;
	e->best_moves = joseki_engine_best_moves;
}
