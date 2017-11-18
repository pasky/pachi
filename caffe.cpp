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
#include "util.h"

static shared_ptr<Net<float> > net;

bool
caffe_ready()
{
	return net;
}

void
caffe_init()
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
caffe_get_data(float *data, float *result)
{
	int size = 19;
	Blob<float> *blob = new Blob<float>(1, 13, size, size);
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

