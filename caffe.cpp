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
	return (net != NULL);
}

void
caffe_init()
{
	if (net)
		return;

	const char *model_file =   get_data_file("golast19.prototxt");
	const char *trained_file = get_data_file("golast.trained");
	if (open_data_file(model_file) == NULL  ||  open_data_file(trained_file) == NULL) {
		if (DEBUGL(1))
			fprintf(stderr, "No dcnn files found, will not use dcnn code.\n");
    close(model_file);
    close(trained_file);
		return;
	}

	Caffe::set_mode(Caffe::CPU);       
	
	/* Load the network. */
	net.reset(new Net<float>(model_file, TEST));
	net->CopyTrainedLayersFrom(trained_file);
	
	if (DEBUGL(1))
		fprintf(stderr, "%s\n", "Loaded Detlef's 54% dcnn.");
}	


void
caffe_get_data(float *data, float *result, int planes, int size)
{
	Blob<float> *blob = new Blob<float>(1, planes, size, size);
	blob->set_cpu_data(data);
	vector<Blob<float>*> bottom;
	bottom.push_back(blob);
	assert(net);
	const vector<Blob<float>*>& rr = net->Forward(bottom);
	
	for (int i = 0; i < 19 * 19; i++) {
		result[i] = rr[0]->cpu_data()[i];
		if (result[i] < 0.00001)
			result[i] = 0.00001;
	}
	
	delete blob;	
}

	
} /* extern "C" */

