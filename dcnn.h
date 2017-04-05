#ifndef PACHI_DCNN_H
#define PACHI_DCNN_H


#ifdef DCNN

#define DCNN_BEST_N 20

struct tree_node;

void dcnn_get_moves(struct board *b, enum stone color, float result[]);
bool using_dcnn(struct board *b);
void dcnn_quiet_caffe(int argc, char *argv[]);
void dcnn_init();
void find_dcnn_best_moves(struct board *b, float *r, coord_t *best_c, float *best_r, int nbest);
void print_dcnn_best_moves(struct tree_node *node, struct board *b, coord_t *best_c, float *best_r, int nbest);

#else

#define using_dcnn(b)  0
#define dcnn_quiet_caffe(argc, argv) 
#define dcnn_init() 

#endif



#endif /* PACHI_DCNN_H */
