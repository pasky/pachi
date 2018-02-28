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

void
caffe_init()
{
	if (net)  return;
	
	char model_file[256];    get_data_file(model_file, "golast19.prototxt");
	char trained_file[256];  get_data_file(trained_file, "golast.trained");
	if (!file_exists(model_file) || !file_exists(trained_file)) {
		if (DEBUGL(1))  fprintf(stderr, "No dcnn files found, will not use dcnn code.\n");
#ifdef _WIN32
		popup("WARNING: Couldn't find Pachi data files, running without dcnn support !\n");
#endif
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

