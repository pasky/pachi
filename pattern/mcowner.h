#ifndef PACHI_PATTERN_MCOWNER_H
#define PACHI_PATTERN_MCOWNER_H

#include "board.h"
#include "ownermap.h"
#include "playout.h"


/* Fill ownermap for mcowner feature. */
void mcowner_playouts(board_t *b, enum stone color, ownermap_t *ownermap);
/* Faster version with few playouts, don't use for anything reliable. */
void mcowner_playouts_fast(board_t *b, enum stone color, ownermap_t *ownermap);


#endif
