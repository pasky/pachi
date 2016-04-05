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
	const char *model_file =   "movepredict.prototxt";
	const char *trained_file = "movepredict.caffemodel";
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
dcnn_get_moves(float *data, float result[])
{
  int size = 19;
	Blob<float> *blob = new Blob<float>(1,2,size,size);
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
	delete blob;
}
	

} /* extern "C" */

