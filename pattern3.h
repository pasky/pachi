#ifndef PACHI_PATTERN3_H
#define PACHI_PATTERN3_H

/* Fast matching of simple 3x3 patterns. */

#include "board.h"

/* (Note that this is completely independent from the general pattern
 * matching infrastructure in pattern.[ch]. This is fast and simple.) */

/* hash3_t pattern: ignore middle point, 2 bits per intersection (color)
 * plus 1 bit per each direct neighbor => 8*2 + 4 bits. Bitmap point order:
 * 7 6 5    b
 * 4   3  a   9
 * 2 1 0    8   */
/* Value bit 0: black pattern; bit 1: white pattern */

/* XXX: See <board.h> for hash3_t typedef. */

typedef struct {
	hash3_t pattern;
	unsigned char value;
} pattern2p_t;

typedef struct {
	/* In case of a collision, following hash entries are
	 * used. value==0 indicates an unoccupied hash entry. */
	/* The hash indices are zobrist hashes based on p3hashes. */
#define pattern3_hash_bits 19
#define pattern3_hash_size (1 << pattern3_hash_bits)
#define pattern3_hash_mask (pattern3_hash_size - 1)
	pattern2p_t hash[pattern3_hash_size];
} pattern3s_t;

/* Zobrist hashes for the various 3x3 points. */
/* [point][is_atari][color] */
extern hash3_t p3hashes[8][2][S_MAX];

/* Source pattern encoding:
 * X: black;  O: white;  .: empty;  #: edge
 * x: !black; o: !white; ?: any
 *
 * |/=: black in atari/anything but black in atari
 * @/0: white in atari
 * Y/y: black notin atari; Q/q: white notin atari
 *
 * extra X: pattern valid only for one side;
 * middle point ignored. */

void pattern3s_init(pattern3s_t *p, char src[][11], int src_n);

/* Compute pattern3 hash at local position. */
static hash3_t pattern3_hash(board_t *b, coord_t c);

/* Check if we match any 3x3 pattern centered on given move. */
static bool pattern3_move_here(pattern3s_t *p, board_t *b, move_t *m, char *idx);

/* Generate all transpositions of given pattern, stored in an
 * hash3_t[8] array. */
void pattern3_transpose(hash3_t pat, hash3_t (*transp)[8]);

/* Reverse pattern to opposite color assignment. */
static hash3_t pattern3_reverse(hash3_t pat);


static inline hash3_t
pattern3_hash(board_t *b, coord_t c)
{
	hash3_t pat = 0;
	int x = coord_x(c), y = coord_y(c);
	/* Stone info. */
	pat |= (board_atxy(b, x - 1, y - 1) << 14)
		| (board_atxy(b, x, y - 1) << 12)
		| (board_atxy(b, x + 1, y - 1) << 10);
	pat |= (board_atxy(b, x - 1, y) << 8)
		| (board_atxy(b, x + 1, y) << 6);
	pat |= (board_atxy(b, x - 1, y + 1) << 4)
		| (board_atxy(b, x, y + 1) << 2)
		| (board_atxy(b, x + 1, y + 1));
	/* Atari info. */
#define atari_atxy(b, x, y) (group_atxy(b, x, y) && board_group_info(b, group_atxy(b, x, y)).libs == 1)
	pat |= (atari_atxy(b, x, y - 1) << 19);
	pat |= (atari_atxy(b, x - 1, y) << 18)
		| (atari_atxy(b, x + 1, y) << 17);
	pat |= (atari_atxy(b, x, y + 1) << 16);
#undef atari_atxy
	return pat;
}

static inline __attribute__((const)) hash_t
hash3_to_hash(hash3_t pat)
{
	hash_t h = 0;
	static const int ataribits[8] = { -1, 0, -1, 1, 2, -1, 3, -1 };
	for (int i = 0; i < 8; i++) {
		h ^= p3hashes[i][ataribits[i] >= 0 ? (pat >> (16 + ataribits[i])) & 1 : 0][(pat >> (i*2)) & 3];
	}
	return h;
}

static inline bool
pattern3_move_here(pattern3s_t *p, board_t *b, move_t *m, char *idx)
{
#ifdef BOARD_PAT3
	hash3_t pat = b->pat3[m->coord];
#else
	hash3_t pat = pattern3_hash(b, m->coord);
#endif
	hash_t h = hash3_to_hash(pat);
	while (p->hash[h & pattern3_hash_mask].pattern != pat
	       && p->hash[h & pattern3_hash_mask].value != 0)
		h++;
	if (p->hash[h & pattern3_hash_mask].value & m->color) {
		*idx = p->hash[h & pattern3_hash_mask].value >> 2;
		return true;
	} else {
		return false;
	}
}

static inline hash3_t
pattern3_reverse(hash3_t pat)
{
	/* Reverse color assignment - achieved by swapping odd and even bits */
	return ((pat >> 1) & 0x5555) | ((pat & 0x5555) << 1) | (pat & 0xf0000);
}

#endif
