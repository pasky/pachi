#ifndef PACHI_JOSEKI_BASE_H
#define PACHI_JOSEKI_BASE_H

#include "board.h"

/* Single joseki situation - moves for S_BLACK-1, S_WHITE-1. */
struct joseki_pattern {
	/* moves[] is a pass-terminated list or NULL */
	coord_t *moves[2];
};

/* The joseki dictionary for given board size. */
struct joseki_dict {
	int bsize;

#define joseki_hash_bits 20 // 8M w/ 32-bit pointers
#define joseki_hash_mask ((1 << joseki_hash_bits) - 1)
	struct joseki_pattern *patterns;
};

struct joseki_dict *joseki_init(int bsize);
struct joseki_dict *joseki_load(int bsize);
void joseki_done(struct joseki_dict *);

#endif
