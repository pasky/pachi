#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CPU_ONLY 1
#include <caffe/caffe.hpp>
using namespace caffe;

extern "C" {
#include "debug.h"
#include "util.h"

static shared_ptr<Net<float> > net;
static int net_size = 0;		/* board size */

/* Make caffe quiet */
void
quiet_caffe(int argc, char *argv[])
{
	google::InitGoogleLogging(argv[0]);
	google::LogToStderr();
	google::SetStderrLogging(google::NUM_SEVERITIES - 1);
}
	
bool
caffe_ready()
{
	return (net != NULL);
}

static int
caffe_load()
{
	char model_file[256];    get_data_file(model_file, "detlef54.prototxt");
	char trained_file[256];  get_data_file(trained_file, "detlef54.trained");
	if (!file_exists(model_file) || !file_exists(trained_file)) {
 		if (DEBUGL(1))  fprintf(stderr, "Couldn't find dcnn files, aborting.\n");
 #ifdef _WIN32
		popup("ERROR: Couldn't find Pachi data files.\n");
 #endif
		exit(1);
	}
	
	Caffe::set_mode(Caffe::CPU);       
	
	/* Load the network. */
	net.reset(new Net<float>(model_file, TEST));
	net->CopyTrainedLayersFrom(trained_file);
	
	/* Get board size. */
	static const vector<int>& shape = net->input_blobs()[0]->shape();
	assert(shape.size() == 4);
	assert(shape[0] == 1 && shape[2] == shape[3]);
	net_size = shape[3];

	return 1;
}

void
caffe_init(int size)
{
	if (net && net_size == size)  return;   /* Nothing to do. */
	if (!net && !caffe_load())    return;
	
	/* Network is fully convolutional so can handle any boardsize,
	 * just need to resize the input layer. */
	if (net_size != size) {
		static const vector<int>& shape = net->input_blobs()[0]->shape();
		net->input_blobs()[0]->Reshape(shape[0], shape[1], size, size);
		net->Reshape();   /* Forward the dimension change. */
		net_size = size;
	}
	
	if (DEBUGL(1))
		fprintf(stderr, "Loaded Detlef's 54%% dcnn for %ix%i\n", size, size);
}


void
caffe_get_data(float *data, float *result, int planes, int size)
{
	assert(net && net_size == size);	
	Blob<float> *blob = new Blob<float>(1, planes, size, size);
	blob->set_cpu_data(data);
	vector<Blob<float>*> bottom;
	bottom.push_back(blob);
	const vector<Blob<float>*>& rr = net->Forward(bottom);
	
	for (int i = 0; i < size * size; i++) {
		result[i] = rr[0]->cpu_data()[i];
		if (result[i] < 0.00001)
			result[i] = 0.00001;
	}
	
	delete blob;	
}

	
} /* extern "C" */

