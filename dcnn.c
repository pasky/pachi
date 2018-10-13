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
void disable_dcnn(void)     {  dcnn_enabled = false;  }
void require_dcnn(void)     {  dcnn_required = true;  dcnn_init();  }

bool
using_dcnn(struct board *b)
{
	bool r = (dcnn_enabled && real_board_size(b) == 19 && caffe_ready());
	if (dcnn_required && !r)  die("dcnn required but not used, aborting.\n");
	return r;
}

void
dcnn_init(void)
{
	if (dcnn_enabled)  caffe_init();
	if (dcnn_required && !caffe_ready())  die("dcnn required, aborting.\n");
}

void
dcnn_get_moves(struct board *b, enum stone color, float result[])
{
	double time_start = time_now();
	assert(real_board_size(b) == 19);

	int dsize = 13 * 19 * 19;
	float *data = malloc(dsize * sizeof(float));
	for (int i = 0; i < dsize; i++)  /* memset() not recommended for floats */
		data[i] = 0;

	for (int x = 0; x < 19; x++)
	for (int y = 0; y < 19; y++) {
		int p = y * 19 + x;

		coord_t c = coord_xy(b, x+1, y+1);
		group_t g = group_at(b, c);
		enum stone bc = board_at(b, c);
		int libs = board_group_info(b, g).libs - 1;
		if (libs > 3) libs = 3;
		if (bc == S_NONE)
			data[       8*19*19 + p] = 1.0;
		else if (bc == color)
			data[(0+libs)*19*19 + p] = 1.0;
		else if (bc == stone_other(color))
			data[(4+libs)*19*19 + p] = 1.0;
		
		if (c == b->last_move.coord)
			data[9*19*19 + p] = 1.0;
		else if (c == b->last_move2.coord)
			data[10*19*19 + p] = 1.0;
		else if (c == b->last_move3.coord)
			data[11*19*19 + p] = 1.0;
		else if (c == b->last_move4.coord)
			data[12*19*19 + p] = 1.0;
	}

	caffe_get_data(data, result, 13, 19);
	free(data);
	if (DEBUGL(2))  fprintf(stderr, "dcnn in %.2fs\n", time_now() - time_start);
}


void
find_dcnn_best_moves(struct board *b, float *r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++)
		best_c[i] = pass;

	foreach_free_point(b) {
		int k = coord2dcnn_idx(c, b);
		best_moves_add(c, r[k], best_c, best_r, nbest);
	} foreach_free_point_end;
}

void
print_dcnn_best_moves(struct board *b, coord_t *best_c, float *best_r, int nbest)
{
	int cols = fprintf(stderr, "dcnn = [ ");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3s ", coord2sstr(best_c[i], b));
	fprintf(stderr, "]\n");

	fprintf(stderr, "%*s[ ", cols-2, "");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3i ", (int)(best_r[i] * 100));
	fprintf(stderr, "]\n");
}
