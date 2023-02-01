#ifndef PACHI_JOSEKI_H
#define PACHI_JOSEKI_H

#include "debug.h"
#include "board.h"
#include "pattern/spatial.h"

#define JOSEKI_PATTERN_DIST	     9

#define joseki_spatial_hash(b, coord, color)         (outer_spatial_hash_from_board_rot_d((b), (coord), (enum stone)(color), 0, JOSEKI_PATTERN_DIST))
#define joseki_3x3_spatial_hash(b, coord, color)     (outer_spatial_hash_from_board_rot_d((b), (coord), (enum stone)(color), 0, 3))

#define JOSEKI_FLAGS_IGNORE  (1 << 0)
#define JOSEKI_FLAGS_3X3     (1 << 1)
#define JOSEKI_FLAGS_LATER   (1 << 2)

typedef struct josekipat {
	short   coord;
	uint8_t color;
	uint8_t flags;
	hash_t  h;	/* full hash */
	struct josekipat *prev;
	
	struct josekipat *next;  /* next hash table entry */
} josekipat_t;

#define josekipat(coord, color, h, prev, flags) \
	{  (short)(coord), (color), (uint8_t)(flags), (h), (prev)  }

#define joseki_hash_bits 18  /* 1Mb w/ 32-bit pointers */
#define joseki_hash_mask ((1 << joseki_hash_bits) - 1)

/* The joseki dictionary for given board size. */
typedef struct {
	int bsize;
	josekipat_t *hash[1 << joseki_hash_bits];  /* regular patterns hashtable */
	josekipat_t *pat_3x3[S_MAX];               /* 3x3 only patterns */
	josekipat_t *ignored;                      /* ignored patterns (linked list) */
} joseki_dict_t;

extern joseki_dict_t *joseki_dict;

/* Enable / disable joseki component */
void disable_joseki();
void require_joseki();

bool using_joseki(board_t *b);
void joseki_load(int bsize);
void joseki_done();
josekipat_t *joseki_add(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color, josekipat_t *prev, int flags);
josekipat_t *joseki_lookup(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color);
josekipat_t *joseki_lookup_ignored(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color);
josekipat_t *joseki_lookup_3x3(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color);
int  joseki_list_moves(joseki_dict_t *jd, board_t *b, enum stone color, coord_t *coords, float *ratings);
void joseki_rate_moves(joseki_dict_t *jd, board_t *b, enum stone color, float *map);
void get_joseki_best_moves(board_t *b, coord_t *coords, float *ratings, int matches, coord_t *best_c, float *best_r, int nbest);
void print_joseki_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest);
void print_joseki_moves(joseki_dict_t *jd, board_t *b, enum stone color);



/* Iterate over all dictionary patterns. */
#define forall_joseki_patterns(jd) \
	for (unsigned int id = 0; id < (1 << joseki_hash_bits); id++) \
		for (josekipat_t *p = (jd)->hash[id]; p; p = p->next)

#define forall_3x3_joseki_patterns(jd) \
	for (int _color = S_BLACK; _color <= S_WHITE; _color++) \
		for (josekipat_t *p = jd->pat_3x3[_color]; p; p = p->next)

#define forall_ignored_joseki_patterns(jd) \
	for (josekipat_t *p = (jd)->ignored; p; p = p->next)


#endif
