#ifndef ZZGO_PATTERN3_H
#define ZZGO_PATTERN3_H

/* Fast matching of simple 3x3 patterns. */

#include "tactics.h"

/* (Note that this is completely independent from the general pattern
 * matching infrastructure in pattern.[ch]. This is fast and simple.) */

struct board;
struct move;

struct pattern3s {
	/* Hashtable: 2*8 bits (ignore middle point, 2 bits per intersection) */
	/* Value: 0: no pattern, 1: black pattern,
	 * 2: white pattern, 3: both patterns */
	char hash[65536];
};

/* Source pattern encoding:
 * X: black;  O: white;  .: empty;  #: edge
 * x: !black; o: !white; ?: any
 *
 * extra X: pattern valid only for one side;
 * middle point ignored. */

void pattern3s_init(struct pattern3s *p, char src[][11], int src_n);

/* Compute pattern3 hash at local position. */
static int pattern3_hash(struct board *b, coord_t c);

/* Check if we match any pattern centered on given move; includes
 * self-atari test. */
static bool test_pattern3_here(struct pattern3s *p, struct board *b, struct move *m);


static inline int
pattern3_hash(struct board *b, coord_t c)
{
	int pat = 0;
	int x = coord_x(c, b), y = coord_y(c, b);
	pat |= (board_atxy(b, x - 1, y - 1) << 14)
		| (board_atxy(b, x, y - 1) << 12)
		| (board_atxy(b, x + 1, y - 1) << 10);
	pat |= (board_atxy(b, x - 1, y) << 8)
		| (board_atxy(b, x + 1, y) << 6);
	pat |= (board_atxy(b, x - 1, y + 1) << 4)
		| (board_atxy(b, x, y + 1) << 2)
		| (board_atxy(b, x + 1, y + 1));
	return pat;
}

/* TODO: Make use of the incremental spatial matching infrastructure
 * in board.h? */
static inline bool
test_pattern3_here(struct pattern3s *p, struct board *b, struct move *m)
{
	int pat = pattern3_hash(b, m->coord);
	//fprintf(stderr, "(%d,%d) hashtable[%04x] = %d\n", x, y, pat, p->hash[pat]);
	return (p->hash[pat] & m->color) && !is_bad_selfatari(b, m->color, m->coord);
}

#endif
