#ifndef PACHI_DCNN_H
#define PACHI_DCNN_H


#ifdef DCNN

#define DCNN_BEST_N 20

/* Don't try to load dcnn. */
void disable_dcnn();

void dcnn_get_moves(struct board *b, enum stone color, float result[]);
bool using_dcnn(struct board *b);
void dcnn_quiet_caffe(int argc, char *argv[]);
void dcnn_init();
void find_dcnn_best_moves(struct board *b, float *r, coord_t *best_c, float *best_r, int nbest);
void print_dcnn_best_moves(struct board *b, coord_t *best_c, float *best_r, int nbest);

/* Time spent in dcnn code */
double get_dcnn_time();
void reset_dcnn_time();

/* Convert board coord to dcnn data index */
static inline int coord2dcnn_idx(coord_t c, struct board *b);


static inline int
coord2dcnn_idx(coord_t c, struct board *b)
{
	int x = coord_x(c, b) - 1;
	int y = coord_y(c, b) - 1;
	return (y * 19 + x);
}


#else


#define disable_dcnn()
#define using_dcnn(b)  0
#define dcnn_quiet_caffe(argc, argv) 
#define dcnn_init()
#define get_dcnn_time()    (0.)
#define reset_dcnn_time()  do { } while(0)


#endif



#endif /* PACHI_DCNN_H */
