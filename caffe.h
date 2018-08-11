
#ifndef PACHI_CAFFE_H
#define PACHI_CAFFE_H

#ifdef __cplusplus
extern "C" {
#endif


bool caffe_ready();
void caffe_init(int size);
void caffe_get_data(float *data, float *result, int planes, int size);

#ifdef DCNN
void quiet_caffe(int argc, char *argv[]);
#else
#define quiet_caffe(argc, argv)
#endif


#ifdef __cplusplus
}
#endif

#endif /* PACHI_CAFFE_H */
