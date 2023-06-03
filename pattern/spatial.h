#ifndef PACHI_PATTERNSP_H
#define PACHI_PATTERNSP_H

/* Matching of spatial pattern features. */

#include "board.h"
#include "move.h"
#include "pattern/pattern.h"

/* Spatial stone configuration pattern features - like pattern3 handles
 * 3x3-area, this handles general N-area (where N is distance in
 * gridcular metric). These routines define the dictionary of spatial
 * configurations (accessible by zobrist hashes or indices) and related
 * data structures; eventually, they support the FEAT_SPATIAL pattern
 * feature implementation in the General Pattern Matcher (pattern.[ch]). */

/* Maximum spatial pattern diameter. */
#define MAX_PATTERN_DIST 10
/* Maximum number of points in spatial pattern (upper bound).
 * TODO: Better upper bound to save more data. */
#define MAX_PATTERN_AREA (MAX_PATTERN_DIST*MAX_PATTERN_DIST)

/* For each encountered configuration of stones, we keep it "spelled out"
 * in the spatial dictionary records, index them and refer just the indices
 * in the feature payloads. This achieves several things:
 * * We can handle patterns of arbitrary length.
 * * We can recognize isomorphous configurations (color reversions,
 *   rotations) within the dataset.
 * * We can visualise patterns corresponding to chosen features.
 *
 * Thus, it goes like this:
 *
 * +----------------+   +----------------+
 * | struct pattern | - | struct feature |
 * +----------------+   |  payload    id |
 *                      +----------------+
 *                            |       FEAT_SPATIAL
 *                            |
 *                            |   ,--<--.
 *                            |   |     |
 * +-----------------------------------------+
 * | struct spatial_dict  spatials[]  hash[] |
 * +-----------------------------------------+
 *                            |
 *                    +----------------+
 *                    | struct spatial |
 *                    +----------------+
 */


/* Spatial record - single stone configuration.
 * Each point is two bits, corresponding to enum stone.
 * Points are ordered in gridcular-defined spiral from middle to the edge.
 * The dictionary file has a comment describing the ordering at the top. 
 *
 *                                                        9
 *   d=2      1           d=3   6  1  5         d=4    6  1  5
 *         4  0  3              4  0  3	            12 4  0  3 11
 *            2                 8  2  7	               8  2  7
 *                                                        10
 *
 *   d=5                         d=10           61
 *          14 9  13                      64 50 37 49 63
 *       18 6  1  5  17                54 40 30 21 29 39 53
 *       12 4  0  3  11             68 44 24 14 9  13 23 43 67
 *       20 8  2  7  19             58 34 18 6  1  5  17 33 57
 *          16 10 15             72 48 28 12 4  0  3  11 27 47 71
 *                                  60 36 20 8  2  7  19 35 59
 *   d=6       21                   70 46 26 16 10 15 25 45 69
 *       24 14 9  13 23                56 42 32 22 31 41 55
 *       18 6  1  5  17                   66 52 38 51 65
 *    28 12 4  0  3  11 27                      62
 *       20 8  2  7  19
 *       26 16 10 15 25
 *             22
 */
 typedef struct {
	unsigned char dist;		/* Gridcular radius of matched pattern. */
	unsigned char points[MAX_PATTERN_AREA / 4];
} spatial_t;

#define spatial_point_at(s, i) ((enum stone)(((s).points[(i) / 4] >> (((i) % 4) * 2)) & 3))


/* Spatial dictionary - collection of stone configurations. */

#ifndef GENSPATIAL
#define spatial_hash_bits 20 // 4Mb array
#else
#define spatial_hash_bits 26 // ~256Mb, need large dict when scanning spatials
#endif
#define spatial_hash_mask ((1 << spatial_hash_bits) - 1)

typedef struct spatial_entry {
	hash_t hash;			/* full hash */
	unsigned int id;		/* spatial record index */
	struct spatial_entry *next;	/* next entry with same hash */
} spatial_entry_t;

typedef struct {
	/* Indexed base store */
	unsigned int nspatials; /* Number of records. */
	spatial_t *spatials; /* Actual records. */
	
	/* number of spatials for each dist, for mm tool */
	unsigned int     nspatials_by_dist[MAX_PATTERN_DIST+1];

	/* Hashed access (all isomorphous configurations are also hashed)
	 * Maps to spatials[] indices. Hash function: zobrist hashing with fixed values. */
	spatial_entry_t* hashtable[1 << spatial_hash_bits];
} spatial_dict_t;

extern spatial_dict_t *spat_dict;
extern const char *spatial_dict_filename;

#define spatial_id(s, dict)  ((unsigned int)((s) - (dict)->spatials))
#define spatial(id, dict)    ((dict)->spatials + (id))


/* Fill up the spatial record from @m vincinity, up to full distance
 * given by pattern config. */
void spatial_from_board(pattern_config_t *pc, spatial_t *s, board_t *b, move_t *m);

/* Compute hash of given spatial pattern. */
hash_t spatial_hash(unsigned int rotation, spatial_t *s);

/* Compute spatial hash from board, ignoring center stone */
hash_t outer_spatial_hash_from_board(board_t *b, coord_t coord, enum stone color);
hash_t outer_spatial_hash_from_board_rot(board_t *b, coord_t coord, enum stone color, int rot);
hash_t outer_spatial_hash_from_board_rot_d(board_t *b, coord_t coord, enum stone color, int rot, unsigned int d);

/* Convert given spatial pattern to string. */
char *spatial2str(spatial_t *s);

/* Print spatial on board centered on @at.
 * Board content is irrelevant, only pattern area is printed:
 *         A B C D E F G H J K L M N O P Q R S T  
 *       +---------------------------------------+
 *    19 |     .                                 |
 *    18 | . . . . .                             |
 *    17 | . . . X X O                           |
 *    16 | . . X . O O .                         |
 *    15 | . . . . . . .                         |
 *    14 | . . .). . . . .                       |
 *    13 | . . . . . . .                         |
 *    12 | . . . . . . .                         |
 *    11 | . . . . . .                           |
 *    10 | . . . . .                             |
 *     9 |     .                                 |
 *     8 |                                       |
 *     7 |                                       |
 *     6 |                                       |
 *     5 |                                       |
 *     4 |                                       |
 *     3 |                                       |
 *     2 |                                       |
 *     1 |                                       |
 *       +---------------------------------------+  */
void spatial_print(board_t *b, FILE *f, spatial_t *s, coord_t at);

/* Mapping from point sequence to coordinate offsets (to determine
 * coordinates relative to pattern center). */
typedef struct { int x, y; } ptcoord_t;
extern ptcoord_t ptcoords[MAX_PATTERN_AREA];
/* For each radius, starting index in ptcoords[]. */
extern unsigned int ptind[MAX_PATTERN_DIST + 2];

/* Zobrist hashes used for points in patterns. */
#define PTH__ROTATIONS	8
extern hash_t pthashes[PTH__ROTATIONS][MAX_PATTERN_AREA][S_MAX];

#define ptcoords_at(x, y, cx, cy, j)		 \
	int x = cx + ptcoords[j].x; \
	int y = cy + ptcoords[j].y; \
	if (x >= the_board_stride()) x = the_board_stride() - 1; else if (x < 0) x = 0; \
	if (y >= the_board_stride()) y = the_board_stride() - 1; else if (y < 0) y = 0;


/* Spatial dictionary file manipulation. */

/* Initializes spatial dictionary, pre-loading existing records from
 * default filename if exists. If create is true, it will not complain
 * about non-existing file and initialize the dictionary anyway.
 * If hash is true, loaded spatials will be added to the hashtable;
 * use false if this is to be done later (e.g. by patternprob). */
void spatial_dict_init(pattern_config_t *pc, bool create);

/* Free spatial dictionary. */
void spatial_dict_done();

/* Lookup spatial pattern (resolves collisions). */
spatial_t *spatial_dict_lookup(spatial_dict_t *dict, int dist, hash_t spatial_hash);

/* Store specified spatial pattern in the dictionary if it is not known yet.
 * Returns spatial id. */
unsigned int spatial_dict_add(spatial_dict_t *dict, spatial_t *s);

/* Write comment lines describing the dictionary (e.g. point order
 * in patterns) to given file. */
void spatial_dict_writeinfo(spatial_dict_t *dict, FILE *f);

/* Append specified spatial pattern to the given file. */
void spatial_write(spatial_dict_t *dict, spatial_t *s, unsigned int id, FILE *f);

#endif
