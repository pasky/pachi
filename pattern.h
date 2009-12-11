#ifndef ZZGO_PATTERN_H
#define ZZGO_PATTERN_H

/* Matching of multi-featured patterns. */

#include "board.h"
#include "move.h"

/* When someone says "pattern", you imagine a configuration of stones in given
 * area (e.g. as matched very efficiently by pattern3 in case of 3x3 area).
 * However, we use a richer definition of pattern, where this is merely one
 * pattern _feature_. Another features may be is-a-selfatari, is-a-capture,
 * number of liberties, distance from last move, etc. */

/* Each feature is represented by its id and an optional 32-bit payload;
 * when matching, discrete (id,payload) pairs are considered. */

/* This is heavily influenced by (Coulom, 2007), of course. */
/* TODO: Try completely separate ko / no-ko features. */

enum feature_id {
	/* Implemented: */

	/* This is a pass. */
	/* Payload: [bit0] Last move was also pass? */
#define PF_PASS_LASTPASS	0
	FEAT_PASS,

	/* Simple capture move. */
	/* Payload: [bit0] Capturing laddered group? */
#define PF_CAPTURE_LADDER	0
	/*          [bit1] Re-capturing last move? */
#define PF_CAPTURE_RECAPTURE	1 /* TODO */
	/*          [bit2] Enables our atari group get more libs? */
#define PF_CAPTURE_ATARIDEF	2
	/*          [bit3] Capturing ko? */
#define PF_CAPTURE_KO		3
	FEAT_CAPTURE,

	/* Atari escape (extension). */
	/* Payload: [bit0] Escaping with laddered group? */
#define PF_AESCAPE_LADDER	0
	FEAT_AESCAPE,

	/* Self-atari move. */
	/* Payload: [bit0] Also using our complex definition? */
#define PF_SELFATARI_SMART	0 /* TODO: Non-smart */
	FEAT_SELFATARI,

	/* Atari move. */
	/* Payload: [bit0] The atari'd group gets laddered? */
#define PF_ATARI_LADDER		0
	/*          [bit1] Playing ko? */
#define PF_ATARI_KO		1
	FEAT_ATARI,

	/* Border distance. */
	/* Payload: The distance - "line number". Only up to 4. */
	FEAT_BORDER,

	/* Last move distance. */
	/* Payload: The distance - gridcular metric. */
	FEAT_LDIST,

	/* Next-to-last move distance. */
	/* Payload: The distance - gridcular metric. */
	FEAT_LLDIST,

	/* Spatial configuration of stones in certain board area. */
	/* Payload: [bits 31-24] Pattern radius (gridcular) */
#define PF_SPATIAL_RADIUS	24
	/*          [bit 23]     Who to play? (1: White) */
#define PF_SPATIAL_TOPLAY	23
	/*          [other bits] Index in the spatial_dict. */
#define PF_SPATIAL_INDEX	0
	FEAT_SPATIAL,


	/* Unimplemented - TODO: */

	/* Monte-carlo owner. */
	/* Payload: #of playouts owning this point at the final
	 * position, scaled to 0..15 (lowest 4 bits). */
	FEAT_MCOWNER,
};

struct feature {
	enum feature_id id;
	uint32_t payload;
};

struct pattern {
	/* Pattern (matched) is set of features. */
	int n;
#define FEATURES 32
	struct feature f[FEATURES];
};

struct spatial_dict;
struct pattern_config {
	/* FEAT_SPATIAL: Generate patterns only for these sizes (gridcular). */
	int spat_min, spat_max;
	/* FEAT_BORDER: Generate features only up to this board distance. */
	int bdist_max;
	/* FEAT_LDIST, FEAT_LLDIST: Generate features only for these move
	 * distances. */
	int ldist_min, ldist_max;
	/* FEAT_MCOWNER: Generate feature after this number of simulations. */
	int mcsims;

	/* The spatial patterns dictionary, used by FEAT_SPATIAL. */
	struct spatial_dict *spat_dict;
};
extern struct pattern_config DEFAULT_PATTERN_CONFIG;


/* Append feature to string. */
char *feature2str(char *str, struct feature *f);
/* Convert string to feature, return pointer after the featurespec. */
char *str2feature(char *str, struct feature *f);

/* Append pattern as feature spec string. */
char *pattern2str(char *str, struct pattern *p);

/* Initialize p and fill it with features matched by the
 * given board move. */
void pattern_match(struct pattern_config *pc, struct pattern *p, struct board *b, struct move *m);


/* Spatial pattern dictionary. */

/* For each encountered configuration of stones, we keep it "spelled out"
 * in these records, index them and refer just the indices in the feature
 * payloads. This achieves several things:
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

/* Maximum spatial pattern diameter. */
#define MAX_PATTERN_DIST 21
/* Maximum number of points in spatial pattern (upper bound).
 * TODO: Better upper bound to save more data. */
#define MAX_PATTERN_AREA (MAX_PATTERN_DIST*MAX_PATTERN_DIST)

/* Record for single stone configuration. */
struct spatial {
	/* Gridcular radius of matched pattern. */
	uint16_t dist;
	/* The points; each point is two bits, corresponding
	 * to {enum stone}. Points are ordered in gridcular-defined
	 * spiral from middle to the edge; the dictionary file has
	 * a comment describing the ordering at the top. */
	char points[MAX_PATTERN_AREA / 4];
#define spatial_point_at(s, i) (((s).points[(i) / 4] >> (((i) % 4) * 2)) & 3)
};

/* Collection of stone configurations, with two ways of lookup:
 * (i) by index (ii) by hash of the configuration. */
struct spatial_dict {
	/* Indexed base store */
	int nspatials; /* Number of records. */
	struct spatial *spatials; /* Actual records. */

	/* Hashed access; all isomorphous configurations
	 * are also hashed */
#define spatial_hash_bits 18
#define spatial_hash_mask ((1 << spatial_hash_bits) - 1)
	/* Maps to spatials[] indices. The hash function
	 * used is zobrist hashing with fixed values. */
	uint32_t hash[1 << spatial_hash_bits];
	/* Auxiliary collision counter, for statistics. */
	int collisions;

	/* Backing store for appending patterns. */
	FILE *f;
};

/* Initializes spatial dictionary, pre-loading existing records from
 * default filename if exists. If will_append is true, it will open
 * the file for appending. */
struct spatial_dict *spatial_dict_init(bool will_append);

/* Lookup specified spatial pattern in the dictionary; return index
 * of the pattern. If the pattern is not found, in read-only mode
 * -1 will be returned, in append mode it will be added both to the
 * dictionary and its file. */
int spatial_dict_get(struct spatial_dict *dict, struct spatial *s);

#endif
