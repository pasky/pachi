
#ifndef PACHI_CAFFE_H
#define PACHI_CAFFE_H

#ifdef __cplusplus
extern "C" {
#endif


bool caffe_ready();
void caffe_init();
void caffe_get_data(float *data, float *result);

#ifdef __cplusplus
}
#endif

#endif /* PACHI_CAFFE_H */
