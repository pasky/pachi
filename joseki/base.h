#ifndef ZZGO_JOSEKI_BASE_H
#define ZZGO_JOSEKI_BASE_H

#include "board.h"

/* Single joseki situation - moves for S_BLACK-1, S_WHITE-1. */
struct joseki {
	/* moves[] is a pass-terminated list or NULL */
	coord_t *moves[2];
};

#define joseki_hash_bits 20 // 8M w/ 32-bit pointers
#define joseki_hash_mask ((1 << joseki_hash_bits) - 1)
extern struct joseki joseki_pats[];

#endif
