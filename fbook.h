#ifndef PACHI_FBOOK_H
#define PACHI_FBOOK_H

#include "move.h"

/* Opening book (fbook as in "forcing book" since the move is just
 * played unconditionally if found, or possibly "fuseki book"). */

typedef struct fbook {
	int bsize;
	int handicap;

	int movecnt;

#define fbook_hash_bits 20 // 12M w/ 32-bit coord_t
#define fbook_hash_mask ((1 << fbook_hash_bits) - 1)
	/* pass == no move in this position */
	coord_t moves[1<<fbook_hash_bits];
	hash_t hashes[1<<fbook_hash_bits];
} fbook_t;

coord_t  fbook_check(board_t *board);
fbook_t* fbook_init(char *filename, board_t *b);
void     fbook_done(fbook_t *fbook);

#endif
