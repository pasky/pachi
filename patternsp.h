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
#define MAX_PATTERN_DIST 7
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

struct spatial {
	/* Gridcular radius of matched pattern. */
	unsigned char dist;
	/* The points; each point is two bits, corresponding
	 * to {enum stone}. Points are ordered in gridcular-defined
	 * spiral from middle to the edge; the dictionary file has
	 * a comment describing the ordering at the top. */
	unsigned char points[MAX_PATTERN_AREA / 4];
#define spatial_point_at(s, i) (((s).points[(i) / 4] >> (((i) % 4) * 2)) & 3)
};

/* Fill up the spatial record from @m vincinity, up to full distance
 * given by pattern config. */
struct pattern_config;
void spatial_from_board(struct pattern_config *pc, struct spatial *s, struct board *b, struct move *m);

/* Compute hash of given spatial pattern. */
hash_t spatial_hash(unsigned int rotation, struct spatial *s);

/* Convert given spatial pattern to string. */
char *spatial2str(struct spatial *s);

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

/* Spatial dictionary - collection of stone configurations. */

/* Two ways of lookup: (i) by index (ii) by hash of the configuration. */
struct spatial_dict {
	/* Indexed base store */
	unsigned int nspatials; /* Number of records. */
	struct spatial *spatials; /* Actual records. */

	/* Hashed access; all isomorphous configurations
	 * are also hashed */
#define spatial_hash_bits 26 // ~256mib array
#define spatial_hash_mask ((1 << spatial_hash_bits) - 1)
	/* Maps to spatials[] indices. The hash function
	 * used is zobrist hashing with fixed values. */
	uint32_t hash[1 << spatial_hash_bits];
	/* Auxiliary counters for statistics. */
	int fills, collisions;
};

/* Initializes spatial dictionary, pre-loading existing records from
 * default filename if exists. If will_append is true, it will not
 * complain about non-existing file and initialize the dictionary anyway.
 * If hash is true, loaded spatials will be added to the hashtable;
 * use false if this is to be done later (e.g. by patternprob). */
struct spatial_dict *spatial_dict_init(bool will_append, bool hash);

/* Lookup specified spatial pattern in the dictionary; return index
 * of the pattern. If the pattern is not found, 0 will be returned. */
static unsigned int spatial_dict_get(struct spatial_dict *dict, int dist, hash_t h);

/* Store specified spatial pattern in the dictionary if it is not known yet.
 * Returns pattern id. Note that the pattern is NOT written to the underlying
 * file automatically. */
unsigned int spatial_dict_put(struct spatial_dict *dict, struct spatial *s, hash_t);

/* Readds given rotation of given pattern to the hash. This is useful only
 * if you want to tweak hash priority of various patterns. */
bool spatial_dict_addh(struct spatial_dict *dict, hash_t hash, unsigned int id);

/* Print stats about the hash to stderr. Companion to spatial_dict_addh(). */
void spatial_dict_hashstats(struct spatial_dict *dict);


/* Spatial dictionary file manipulation. */

/* Loading routine is not exported, it is called automatically within
 * spatial_dict_init(). */

/* Default spatial dict filename to use. */
extern const char *spatial_dict_filename;

/* Write comment lines describing the dictionary (e.g. point order
 * in patterns) to given file. */
void spatial_dict_writeinfo(struct spatial_dict *dict, FILE *f);

/* Append specified spatial pattern to the given file. */
void spatial_write(struct spatial_dict *dict, struct spatial *s, unsigned int id, FILE *f);


static inline unsigned int
spatial_dict_get(struct spatial_dict *dict, int dist, hash_t hash)
{
	unsigned int id = dict->hash[hash];
#ifdef DEBUG
	if (id && dict->spatials[id].dist != dist) {
		if (DEBUGL(6))
			fprintf(stderr, "Collision dist %d vs %d (hash [%d]%"PRIhash")\n",
				dist, dict->spatials[id].dist, id, hash);
		return 0;
	}
#endif
	return id;
}

#endif
