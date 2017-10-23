#define DEBUG
#include <assert.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "caffe.h"
#include "../dcnn.h"
#include "engines/dcnn.h"



static coord_t *
dcnn_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
        float r[19 * 19];
        float best_r[DCNN_BEST_N] = { 0.0, };
        coord_t best_moves[DCNN_BEST_N];
        dcnn_get_moves(b, color, r);
        find_dcnn_best_moves(b, r, best_moves, best_r, DCNN_BEST_N);
        print_dcnn_best_moves(NULL, b, best_moves, best_r, DCNN_BEST_N);
        
        return coord_copy(best_moves[0]);
}       

static void
dcnn_best_moves(struct engine *e, struct board *b, struct time_info *ti, enum stone color, 
		coord_t *best_c, float *best_r, int nbest)
{
	float r[19 * 19];
	dcnn_get_moves(b, color, r);
	find_dcnn_best_moves(b, r, best_c, best_r, nbest);
	print_dcnn_best_moves(NULL, b, best_c, best_r, nbest);
}	

struct engine *
engine_dcnn_init(char *arg, struct board *b)
{
	dcnn_init();
	if (!caffe_ready()) {
		fprintf(stderr, "Couldn't initialize dcnn, aborting.\n");
		abort();
	}
	struct engine *e = (struct engine*)calloc2(1, sizeof(struct engine));
	e->name = (char*)"DCNN Engine";
	e->comment = (char*)"I just select dcnn's best move.";
	e->genmove = dcnn_genmove;
	e->best_moves = dcnn_best_moves;

	return e;
}

