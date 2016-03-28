
#ifndef PACHI_DCNN_H
#define PACHI_DCNN_H

#ifdef __cplusplus
extern "C" {
#endif


#ifdef DCNN

void dcnn_get_moves(struct board *b, enum stone color, float result[]);
bool using_dcnn(struct board *b);
void dcnn_quiet_caffe(int argc, char *argv[]);
void dcnn_init();

#else

#define using_dcnn(b)  0
#define dcnn_quiet_caffe(argc, argv) 
#define dcnn_init() 

#endif


#ifdef __cplusplus
}
#endif

#endif /* PACHI_DCNN_H */
