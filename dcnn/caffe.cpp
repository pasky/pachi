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

static int
shape_size(const vector<int>& shape)
{
       int size = 1;
       for (unsigned int i = 0; i < shape.size(); i++)
               size *= shape[i];
       return size;
}
	
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
caffe_load(char *model, char *weights, int default_size)
{
	char model_file[256];    get_data_file(model_file, model);
	char weights_file[256];  get_data_file(weights_file, weights);
	if (!file_exists(model_file) || !file_exists(weights_file)) {
		if (DEBUGL(1))  fprintf(stderr, "Loading dcnn files: %s, %s\n"
					        "Couldn't find dcnn files, aborting.\n", model, weights);
#ifdef _WIN32
		popup("ERROR: Couldn't find Pachi data files.\n");
#endif
		exit(1);
	}
	
	Caffe::set_mode(Caffe::CPU);       
	
	/* Load the network. */
	net.reset(new Net<float>(model_file, TEST));
	net->CopyTrainedLayersFrom(weights_file);
	net_size = default_size;

	return 1;
}

void
caffe_init(int size, char *model, char *weights, char *name, int default_size)
{
	if (net && net_size == size)  return;   /* Nothing to do. */
	if (!net && !caffe_load(model, weights, default_size))    return;
	
	/* If network is fully convolutional it can handle any boardsize,
	 * just need to resize the input layer. */
	if (net_size != size) {
		static const vector<int>& shape = net->input_blobs()[0]->shape();
		net->input_blobs()[0]->Reshape(shape[0], shape[1], size, size);
		net->Reshape();   /* Forward the dimension change. */
		net_size = size;
	}
	
	if (DEBUGL(1))
		fprintf(stderr, "Loaded %s dcnn for %ix%i\n", name, size, size);
}

void
caffe_done()
{
	net.reset();
	net_size = 0;
}
	
void
caffe_get_data(float *data, float *result, int size, int planes, int psize)
{
	assert(net && net_size == size);
	Blob<float> *blob = new Blob<float>(1, planes, psize, psize);
	blob->set_cpu_data(data);
	vector<Blob<float>*> bottom;
	bottom.push_back(blob);
	const vector<Blob<float>*>& rr = net->Forward(bottom);
	assert(shape_size(rr[0]->shape()) >= size * size);
	
	for (int i = 0; i < size * size; i++) {
		result[i] = rr[0]->cpu_data()[i];
		if (result[i] < 0.00001)
			result[i] = 0.00001;
	}
	
	delete blob;
}

	
} /* extern "C" */

