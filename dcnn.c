#define DEBUG
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "uct/tree.h"
#include "caffe.h"
#include "dcnn.h"
#include "timeinfo.h"

static bool dcnn_enabled = true;
static bool dcnn_required = false;
void disable_dcnn(void)  {  dcnn_enabled = false;  }
void require_dcnn(void)  {  dcnn_required = true;  }

static void detlef54_dcnn_eval(board_t *b, enum stone color, float result[]);

static bool
dcnn_supported_board_size(board_t *b)
{
	return (real_board_size(b) >= 13);
}

bool
using_dcnn(board_t *b)
{
	bool r = dcnn_enabled && dcnn_supported_board_size(b) && caffe_ready();
	if (dcnn_required && !r)  die("dcnn required but not used, aborting.\n");
	return r;
}

void
dcnn_init(board_t *b)
{
	if (dcnn_enabled && dcnn_supported_board_size(b))
		caffe_init(real_board_size(b));
	if (dcnn_required && !caffe_ready())  die("dcnn required, aborting.\n");
}

void
dcnn_evaluate_quiet(board_t *b, enum stone color, float result[])
{
	detlef54_dcnn_eval(b, color, result);
}

void
dcnn_evaluate(board_t *b, enum stone color, float result[])
{
	double time_start = time_now();	
	detlef54_dcnn_eval(b, color, result);
	if (DEBUGL(2))  fprintf(stderr, "dcnn in %.2fs\n", time_now() - time_start);
}

static void
detlef54_dcnn_eval(board_t *b, enum stone color, float result[])
{
	assert(dcnn_supported_board_size(b));

	int size = real_board_size(b);
	int dsize = 13 * size * size;
	float *data = malloc(dsize * sizeof(float));
	for (int i = 0; i < dsize; i++)  /* memset() not recommended for floats */
		data[i] = 0;

	for (int x = 0; x < size; x++)
	for (int y = 0; y < size; y++) {
		int p = y * size + x;

		coord_t c = coord_xy(x+1, y+1);
		group_t g = group_at(b, c);
		enum stone bc = board_at(b, c);
		int libs = board_group_info(b, g).libs - 1;
		if (libs > 3) libs = 3;
		if (bc == S_NONE)
			data[       8*size*size + p] = 1.0;
		else if (bc == color)
			data[(0+libs)*size*size + p] = 1.0;
		else if (bc == stone_other(color))
			data[(4+libs)*size*size + p] = 1.0;
		
		if (c == b->last_move.coord)
			data[9*size*size + p] = 1.0;
		else if (c == b->last_move2.coord)
			data[10*size*size + p] = 1.0;
		else if (c == b->last_move3.coord)
			data[11*size*size + p] = 1.0;
		else if (c == b->last_move4.coord)
			data[12*size*size + p] = 1.0;
	}

	caffe_get_data(data, result, 13, size);
	free(data);
}


void
get_dcnn_best_moves(board_t *b, float *r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}

	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		best_moves_add(c, r[k], best_c, best_r, nbest);
	} foreach_free_point_end;
}

void
print_dcnn_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest)
{
	int cols = best_moves_print(b, "dcnn = ", best_c, nbest);

	fprintf(stderr, "%*s[ ", cols, "");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3i ", (int)(best_r[i] * 100));
	fprintf(stderr, "]\n");
}

