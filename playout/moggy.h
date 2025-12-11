#ifndef PACHI_PLAYOUT_MOGGY_H
#define PACHI_PLAYOUT_MOGGY_H

#include "playout.h"

/* Use joseki moves in moggy ? */
//#define MOGGY_JOSEKI 1


struct playout_policy *playout_moggy_init(char *arg, board_t *b);

#endif
