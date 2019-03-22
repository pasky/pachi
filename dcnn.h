#ifndef PACHI_DCNN_H
#define PACHI_DCNN_H


#ifdef DCNN

#define DCNN_BEST_N 20

/* Ensure / disable dcnn */
void require_dcnn(void);
void disable_dcnn(void);

void dcnn_evaluate(board_t *b, enum stone color, float result[]);
void dcnn_evaluate_quiet(board_t *b, enum stone color, float result[]);
bool using_dcnn(board_t *b);
void dcnn_init(board_t *b);
void get_dcnn_best_moves(board_t *b, float *r, coord_t *best_c, float *best_r, int nbest);
void print_dcnn_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest);

/* Convert board coord to dcnn data index */
static inline int coord2dcnn_idx(coord_t c);


static inline int
coord2dcnn_idx(coord_t c)
{
	int size = the_board_rsize();
	int x = coord_x(c) - 1;
	int y = coord_y(c) - 1;
	return (y * size + x);
}


#else


#define disable_dcnn()  ((void)0)
#define require_dcnn()  die("dcnn required but not compiled in, aborting.\n")
#define using_dcnn(b)   0
#define dcnn_init(b)    ((void)0)


#endif



#endif /* PACHI_DCNN_H */
