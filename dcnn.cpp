#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CPU_ONLY 1
#include <caffe/caffe.hpp>
using namespace caffe;

extern "C" {
#include "debug.h"
#include "board.h"
#include "dcnn.h"
	
	
static shared_ptr<Net<float> > net;

bool
using_dcnn(struct board *b)
{
	return (real_board_size(b) == 19 && net);
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
dcnn_init()
{
	if (net)
		return;

	struct stat s;	
	const char *model_file =   "golast19.prototxt";
	const char *trained_file = "golast.trained";
	if (stat(model_file, &s) != 0  ||  stat(trained_file, &s) != 0) {
		if (DEBUGL(1))
			fprintf(stderr, "No dcnn files found, will not use dcnn code.\n");
		return;
	}
	
	Caffe::set_mode(Caffe::CPU);       
	
	/* Load the network. */
	net.reset(new Net<float>(model_file, TEST));
	net->CopyTrainedLayersFrom(trained_file);
	
	if (DEBUGL(1))
		fprintf(stderr, "Initialized dcnn.\n");
}

void
dcnn_get_moves(struct board *b, enum stone color, float result[])
{
	assert(real_board_size(b) == 19);

	int size = 19;
	int dsize = 13 * size * size;
	float *data = new float[dsize];
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

	Blob<float> *blob = new Blob<float>(1,13,size,size);
	blob->set_cpu_data(data);
	vector<Blob<float>*> bottom;
	bottom.push_back(blob);
	assert(net);
	const vector<Blob<float>*>& rr = net->Forward(bottom);
	
	for (int i = 0; i < size * size; i++) {
		result[i] = rr[0]->cpu_data()[i];
		if (result[i] < 0.00001)
			result[i] = 0.00001;
	}
	delete[] data;
	delete blob;
}
	

} /* extern "C" */

