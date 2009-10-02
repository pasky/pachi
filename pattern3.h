#ifndef ZZGO_PATTERN3_H
#define ZZGO_PATTERN3_H

/* Fast matching of simple 3x3 patterns. */

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

bool test_pattern3_here(struct pattern3s *p, struct board *b, struct move *m);

#endif
