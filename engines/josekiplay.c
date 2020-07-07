#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "joseki.h"
#include "engines/josekiplay.h"

static void
josekiplay_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
		      coord_t *best_c, float *best_r, int nbest)
{
	coord_t coords[BOARD_MAX_COORDS];
	float ratings[BOARD_MAX_COORDS];
	int matches = joseki_list_moves(joseki_dict, b, color, coords, ratings);
	
	get_joseki_best_moves(b, coords, ratings, matches, best_c, best_r, nbest);
	print_joseki_best_moves(b, best_c, best_r, nbest);
}

static coord_t
josekiplay_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	coord_t best_c[20];
	float   best_r[20];
	josekiplay_best_moves(e, b, ti, color, best_c, best_r, 20);

	return best_c[0];
}

void
engine_josekiplay_init(engine_t *e, board_t *b)
{
	e->name = "JosekiPlay";
	e->comment = "I select joseki moves blindly, if there are none i just pass.";
	e->genmove = josekiplay_genmove;
	e->best_moves = josekiplay_best_moves;
}
