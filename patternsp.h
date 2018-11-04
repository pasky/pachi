#ifndef PACHI_PATTERNSP_H
#define PACHI_PATTERNSP_H

/* Matching of spatial pattern features. */

#include "board.h"
#include "move.h"
#include "pattern.h"

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


/* Spatial record - single stone configuration. */

typedef struct spatial {
	/* Gridcular radius of matched pattern. */
	unsigned char dist;
	/* The points; each point is two bits, corresponding
	 * to {enum stone}. Points are ordered in gridcular-defined
	 * spiral from middle to the edge; the dictionary file has
	 * a comment describing the ordering at the top. */
	unsigned char points[MAX_PATTERN_AREA / 4];
#define spatial_point_at(s, i) (((s).points[(i) / 4] >> (((i) % 4) * 2)) & 3)
} spatial_t;


/* Spatial dictionary - collection of stone configurations. */

extern struct spatial_dict *spat_dict;
extern const char *spatial_dict_filename;

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

typedef struct spatial_dict {
	/* Indexed base store */
	unsigned int nspatials; /* Number of records. */
	struct spatial *spatials; /* Actual records. */
	
	/* number of spatials for each dist, for mm tool */
	unsigned int     nspatials_by_dist[MAX_PATTERN_DIST+1];

	/* Hashed access (all isomorphous configurations are also hashed)
	 * Maps to spatials[] indices. Hash function: zobrist hashing with fixed values. */
	spatial_entry_t* hashtable[1 << spatial_hash_bits];
} spatial_dict_t;

#define spatial_id(s, dict)  ((unsigned int)((s) - (dict)->spatials))
#define spatial(id, dict)    ((dict)->spatials + (id))


/* Fill up the spatial record from @m vincinity, up to full distance
 * given by pattern config. */
struct pattern_config;
void spatial_from_board(struct pattern_config *pc, spatial_t *s, struct board *b, struct move *m);

/* Compute hash of given spatial pattern. */
hash_t spatial_hash(unsigned int rotation, spatial_t *s);

/* Compute spatial hash from board, ignoring center stone */
hash_t outer_spatial_hash_from_board(struct board *b, coord_t coord, enum stone color);
hash_t outer_spatial_hash_from_board_rot(struct board *b, coord_t coord, enum stone color, int rot);
hash_t outer_spatial_hash_from_board_rot_d(struct board *b, coord_t coord, enum stone color, int rot, unsigned int d);

/* Convert given spatial pattern to string. */
char *spatial2str(spatial_t *s);

/* Print spatial on board centered on @at */
void spatial_print(struct board *b, spatial_t *s, FILE *f, struct move *at);

/* Mapping from point sequence to coordinate offsets (to determine
 * coordinates relative to pattern center). */
struct ptcoord { short x, y; } ptcoords[MAX_PATTERN_AREA];
/* For each radius, starting index in ptcoords[]. */
unsigned int ptind[MAX_PATTERN_DIST + 2];

/* Zobrist hashes used for points in patterns. */
#define PTH__ROTATIONS	8
hash_t pthashes[PTH__ROTATIONS][MAX_PATTERN_AREA][S_MAX];

#define ptcoords_at(x_, y_, c_, b_, j_) \
	int x_ = coord_x((c_), (b_)) + ptcoords[j_].x; \
	int y_ = coord_y((c_), (b_)) + ptcoords[j_].y; \
	if (x_ >= board_size(b_)) x_ = board_size(b_) - 1; else if (x_ < 0) x_ = 0; \
	if (y_ >= board_size(b_)) y_ = board_size(b_) - 1; else if (y_ < 0) y_ = 0;


/* Spatial dictionary file manipulation. */

/* Initializes spatial dictionary, pre-loading existing records from
 * default filename if exists. If create is true, it will not complain
 * about non-existing file and initialize the dictionary anyway.
 * If hash is true, loaded spatials will be added to the hashtable;
 * use false if this is to be done later (e.g. by patternprob). */
void spatial_dict_init(struct pattern_config *pc, bool create);

/* Free spatial dictionary. */
void spatial_dict_done();

/* Lookup spatial pattern (resolves collisions). */
spatial_t *spatial_dict_lookup(spatial_dict_t *dict, int dist, hash_t spatial_hash);

/* Store specified spatial pattern in the dictionary if it is not known yet.
 * Returns spatial id. */
unsigned int spatial_dict_add(spatial_dict_t *dict, struct spatial *s);

/* Write comment lines describing the dictionary (e.g. point order
 * in patterns) to given file. */
void spatial_dict_writeinfo(struct spatial_dict *dict, FILE *f);

/* Append specified spatial pattern to the given file. */
void spatial_write(struct spatial_dict *dict, struct spatial *s, unsigned int id, FILE *f);

#endif
