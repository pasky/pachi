
#ifndef PACHI_CAFFE_H
#define PACHI_CAFFE_H


bool caffe_ready(void);
void caffe_init(int size, char *model, char *weights, char *name, int default_size);
void caffe_done(void);
void caffe_get_data(float *data, float *result, int size, int planes, int psize);

#ifdef DCNN
void quiet_caffe(int argc, char *argv[]);
#else
#define quiet_caffe(argc, argv) ((void)0)
#endif


#endif /* PACHI_CAFFE_H */
