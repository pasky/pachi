#define DEBUG
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "uct/tree.h"
#include "caffe.h"
#include "dcnn.h"
	
	
bool
using_dcnn(struct board *b)
{
	return (real_board_size(b) == 19 && caffe_ready());
}

void
dcnn_init()
{
	caffe_init();
}

/* Make caffe quiet */
void
dcnn_quiet_caffe(int argc, char *argv[])
{
	if (DEBUGL(7) || getenv("GLOG_minloglevel"))
		return;
	
	setenv("GLOG_minloglevel", "2", 1);
	execvp(argv[0], argv);   /* Sucks that we have to do this */
}


void
dcnn_get_moves(struct board *b, enum stone color, float result[])
{
	assert(real_board_size(b) == 19);

	int size = 19;
	int dsize = 13 * size * size;
	float *data = malloc(sizeof(float) * dsize);
	for (int i = 0; i < dsize; i++) 
		data[i] = 0.0;

	for (int j = 0; j < size; j++) {
		for(int k = 0; k < size; k++) {
			int p = size * j + k;
			coord_t c = coord_xy(b, j+1, k+1);
			group_t g = group_at(b, c);
			enum stone bc = board_at(b, c);
			int libs = board_group_info(b, g).libs - 1;
			if (libs > 3) libs = 3;
			if (bc == S_NONE) 
				data[8*size*size + p] = 1.0;
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
	}

	caffe_get_data(data, result);
	free(data);
}

void
find_dcnn_best_moves(struct board *b, float *r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++)
		best_c[i] = pass;

	foreach_free_point(b) {
		int k = (coord_x(c, b) - 1) * 19 + (coord_y(c, b) - 1);
		best_moves_add(c, r[k], best_c, best_r, nbest);
	} foreach_free_point_end;
}
	
void
print_dcnn_best_moves(struct tree_node *node, struct board *b,
		      coord_t *best_c, float *best_r, int nbest)
{
	int depth = (node ? node->depth : 0);
	coord_t c = (node ? node_coord(node) : pass);
	fprintf(stderr, "%.*sprior_dcnn(%s) = [ ",
		depth * 4, "                                   ",
		coord2sstr(c, b));
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%s ", coord2sstr(best_c[i], b));
	fprintf(stderr, "]      ");

	fprintf(stderr, "[ ");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%.2f ", best_r[i]);
	fprintf(stderr, "]\n");
}
	
	


