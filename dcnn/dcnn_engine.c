#define DEBUG
#include <assert.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "caffe.h"
#include "dcnn/dcnn.h"
#include "dcnn/dcnn_engine.h"



static coord_t
dcnn_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	float r[19 * 19];	
	dcnn_evaluate(b, color, r, NULL, DEBUGL(2), "");

	float best_r[DCNN_BEST_N];
	coord_t best_moves[DCNN_BEST_N];
	best_moves_setup(best, best_moves, best_r, DCNN_BEST_N);
	get_dcnn_best_moves(b, r, &best);
	
	/* Make sure move is valid ... */
	for (int i = 0; i < best.n; i++) {
		if (is_pass(best_moves[i]) ||
		    board_is_valid_play_no_suicide(b, color, best_moves[i]))
			return best_moves[i];
		die("dcnn suggests invalid move %s !\n", coord2sstr(best_moves[i]));
	}
	
	assert(0);
	return pass;
}

static void
dcnn_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color, best_moves_t *best)
{
	float r[19 * 19];
	dcnn_evaluate(b, color, r, NULL, DEBUGL(2), "");
	get_dcnn_best_moves(b, r, best);
}

#define option_error engine_setoption_error

static bool
dcnn_engine_setoption(engine_t *e, board_t *b, const char *optname, char *optval,
		      char **err, bool setup, bool *reset)
{
	static_strbuf(ebuf, 256);

	if (!strcasecmp(optname, "threads") && optval) {
		/* Set number of threads for dcnn evaluation */
		dcnn_set_threads(atoi(optval));
	}
	else
		option_error("dcnn: Invalid engine argument %s or missing value\n", optname);

	return true;
}

void
dcnn_engine_init(engine_t *e, board_t *b)
{
	e->name = (char*)"DCNN";
	e->comment = (char*)"I just select dcnn's best move.";
	e->genmove = dcnn_genmove;
	e->setoption = dcnn_engine_setoption;
	e->best_moves = dcnn_best_moves;

	/* Process engine options. */
	char *err;
	options_t *options = &e->options;
	for (int i = 0; i < options->n; i++)
		if (!engine_setoption(e, b, &options->o[i], &err, true, NULL))
			die("%s", err);
	
	dcnn_init(b);
	if (!caffe_ready()) {
		fprintf(stderr, "Couldn't initialize dcnn, aborting.\n");
		abort();
	}

}

