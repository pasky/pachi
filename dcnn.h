#ifndef PACHI_DCNN_H
#define PACHI_DCNN_H


#ifdef DCNN

#define DCNN_BEST_N 20

/* Ensure / disable dcnn */
void require_dcnn(void);
void disable_dcnn(void);

void dcnn_get_moves(struct board *b, enum stone color, float result[]);
bool using_dcnn(struct board *b);
void dcnn_init(void);
void find_dcnn_best_moves(struct board *b, float *r, coord_t *best_c, float *best_r, int nbest);
void print_dcnn_best_moves(struct board *b, coord_t *best_c, float *best_r, int nbest);

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
#define require_dcnn()     die("dcnn required but not compiled in, aborting.\n")
#define using_dcnn(b)  0
#define dcnn_init()


#endif



#endif /* PACHI_DCNN_H */
