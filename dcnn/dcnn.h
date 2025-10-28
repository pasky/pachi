#ifndef PACHI_DCNN_H
#define PACHI_DCNN_H


#ifdef DCNN

#include "engine.h"

#define DCNN_BEST_N 20

/* Choose which dcnn to load */
void set_dcnn(char *name);
void list_dcnns(void);
int dcnn_default_board_size(void);

/* Ensure / disable / check dcnn */
void require_dcnn(void);
void disable_dcnn(void);
bool using_dcnn(board_t *b);	/* Only after dcnn_init() */

void disable_dcnn_blunder(void);
void dcnn_blunder_init(void);

/* Set number of threads for dcnn evaluation */
void dcnn_set_threads(int nthreads);
/* Load/reload network for current boardsize */
void dcnn_init(board_t *b);
/* Evaluate dcnn and fix dcnn blunders (if they haven't been disabled) */
void dcnn_evaluate(board_t *b, enum stone color, float result[], ownermap_t *ownermap, bool debugl, char *extra_log);
/* Raw dcnn output (doesn't fix blunders) */
void dcnn_evaluate_raw(board_t *b, enum stone color, float result[], ownermap_t *ownermap, bool debugl, char *extra_log);
/* Get best moves */
void get_dcnn_best_moves(board_t *b, float *r, best_moves_t *best);
void print_dcnn_best_moves(best_moves_t *best);

/* Convert board coord to dcnn data index */
static int coord2dcnn_idx(coord_t c);

/* Private use */
int  dcnn_fix_blunders(board_t *b, enum stone color, float result[], ownermap_t *ownermap, bool debugl);
void get_dcnn_blunders(bool boosted, board_t *b, enum stone color, float result[], ownermap_t *ownermap, mq_t *q);
bool dcnn_first_line_connect_blunder(board_t *b, move_t *m);

extern int darkforest_dcnn;


static inline int
coord2dcnn_idx(coord_t c)
{
	int size = the_board_rsize();
	int x = coord_x(c) - 1;
	int y = coord_y(c) - 1;
	return (y * size + x);
}


#else


#define set_dcnn(n)		die("dcnn required but not compiled in, aborting.\n")
#define dcnn_default_board_size()  19
#define disable_dcnn()		((void)0)
#define require_dcnn()		die("dcnn required but not compiled in, aborting.\n")
#define using_dcnn(b)		0
#define dcnn_init(b)		((void)0)
#define dcnn_set_threads(n)	((void)0)

#define dcnn_blunder_init()	((void)0)
#define disable_dcnn_blunder()	((void)0)


#endif



#endif /* PACHI_DCNN_H */
