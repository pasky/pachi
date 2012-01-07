#ifndef PACHI_PATTERN_H
#define PACHI_PATTERN_H

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

/* This is heavily influenced by (Coulom, 2007), of course. In addition,
 * the work of van der Werf, de Groot, Stern et al. and possibly others
 * inspired this pattern matcher. */
/* TODO: Try completely separate ko / no-ko features. And many other
 * features described in the literature. */

/* See the HACKING file for another description of the pattern matcher and
 * instructions on how to harvest and inspect patterns. */

/* If you add a payload bit for a feature, don't forget to update the value
 * in feature_info. */
enum feature_id {
	/* Implemented: */

	/* Simple capture move. */
	/* Payload: [bit0] Capturing laddered group? */
#define PF_CAPTURE_LADDER	0
	/*          [bit2] Enables our atari group get more libs? */
#define PF_CAPTURE_ATARIDEF	1
	/*          [bit3] Capturing ko? */
#define PF_CAPTURE_KO		2
	/*          [bit4] Single-stone group? */
#define PF_CAPTURE_1STONE	3
	/*          [bit5] Unsafe move for opponent? */
#define PF_CAPTURE_TRAPPED	4
	/*          [bit6] Preventing connection to an outside group. */
#define PF_CAPTURE_CONNECTION	5
	FEAT_CAPTURE,

	/* Atari escape (extension). */
	/* Payload: [bit0] Escaping with laddered group? */
#define PF_AESCAPE_LADDER	0
	/*          [bit1] Single-stone group? */
#define PF_AESCAPE_1STONE	1
	/*          [bit2] Unsafe move for us? */
#define PF_AESCAPE_TRAPPED	2
	/*          [bit3] Connecting out to an outside group. */
#define PF_AESCAPE_CONNECTION	3
	FEAT_AESCAPE,

	/* Self-atari move. */
	/* Payload: [bit0] Matched by trivial definition? */
	/*          [bit1] Matched by complex definition? (tries to be aware of nakade, throwins, ...) */
#define PF_SELFATARI_STUPID	0
#define PF_SELFATARI_SMART	1
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

	/* Continuity. */
	/* Payload: [bit0] The move is in 8-neighborhood of last move. */
	FEAT_CONTIGUITY,

	/* Spatial configuration of stones in certain board area,
	 * with black to play. */
	/* Payload: Index in the spatial_dict. */
	FEAT_SPATIAL,

	/* TODO: MC owner, playing ko, #liberties, #libs of opponent, ... */

	FEAT_MAX
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
	/* FEAT_BORDER: Generate features only up to this board distance. */
	int bdist_max;

	/* FEAT_SPATIAL: Generate patterns only for these sizes (gridcular).
	 * TODO: Special-case high values to match larger areas or the
	 * whole board. */
	int spat_min, spat_max;
	/* Produce only a single spatial feature per pattern, corresponding
	 * to the largest matched spatial pattern. */
	bool spat_largest;
	/* The spatial patterns dictionary used by FEAT_SPATIAL. */
	struct spatial_dict *spat_dict;
};
extern struct pattern_config DEFAULT_PATTERN_CONFIG;

/* The pattern_spec[] specifies which features to tests for;
 * highest bit controls whether to test for the feature at all,
 * then for bitmap features (except FEAT_SPATIAL) the rest
 * of the bits controls various PF tests; for non-bitmap
 * features, you will need to tweak the patternconfig to
 * fine-tune them. */
typedef uint16_t pattern_spec[FEAT_MAX];
/* Match (almost?) all supported features. */
extern pattern_spec PATTERN_SPEC_MATCH_DEFAULT;


/* Append feature to string. */
char *feature2str(char *str, struct feature *f);
/* Convert string to feature, return pointer after the featurespec. */
char *str2feature(char *str, struct feature *f);
/* Get name of given feature. */
char *feature_name(enum feature_id f);
/* Get number of possible payload values associated with the feature. */
int feature_payloads(struct pattern_config *pc, enum feature_id f);

/* Append pattern as feature spec string. */
char *pattern2str(char *str, struct pattern *p);

/* Initialize p and fill it with features matched by the
 * given board move. */
void pattern_match(struct pattern_config *pc, pattern_spec ps, struct pattern *p, struct board *b, struct move *m);

#endif
