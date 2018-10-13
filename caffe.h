
#ifndef PACHI_CAFFE_H
#define PACHI_CAFFE_H

#ifdef __cplusplus
extern "C" {
#endif


bool caffe_ready(void);
void caffe_init(void);
void caffe_get_data(float *data, float *result, int planes, int size);

#ifdef DCNN
void quiet_caffe(int argc, char *argv[]);
#else
#define quiet_caffe(argc, argv) ((void)0)
#endif


#ifdef __cplusplus
}
#endif

#endif /* PACHI_CAFFE_H */
