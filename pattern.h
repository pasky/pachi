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

/* See the HACKING file for another description of the pattern matcher and
 * instructions on how to harvest and inspect patterns. */

/* If you add a payload bit for a feature, don't forget to update the value
 * in feature_info. */
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
#define PF_SELFATARI_SMART	0
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

	/* Continuity. */
	/* Payload: [bit0] The move is in 8-neighborhood of last move (ldist<=3) */
	/* This is a fast substitution to ldist/lldist. */
	FEAT_CONTIGUITY,

	/* Spatial configuration of stones in certain board area,
	 * with black to play. */
	/* Payload: [other bits] Index in the spatial_dict. */
	FEAT_SPATIAL,


	/* Unimplemented - TODO: */

	/* Monte-carlo owner. */
	/* Payload: #of playouts owning this point at the final
	 * position, scaled to 0..15 (lowest 4 bits). */
	FEAT_MCOWNER,

	FEAT_MAX
};

struct feature {
	enum feature_id id;
	uint16_t payload;
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
extern struct pattern_config FAST_PATTERN_CONFIG;

/* The pattern_spec[] specifies which features to tests for;
 * highest bit controls whether to test for the feature at all,
 * then for bitmap features (except FEAT_SPATIAL) the rest
 * of the bits controls various PF tests; for non-bitmap
 * features, you will need to tweak the patternconfig to
 * fine-tune them. */
typedef uint16_t pattern_spec[FEAT_MAX];
/* Match all supported features. */
extern pattern_spec PATTERN_SPEC_MATCHALL;
/* Match only "quick" features, suitable for MC simulations. */
extern pattern_spec PATTERN_SPEC_MATCHFAST;


/* Append feature to string. */
char *feature2str(char *str, struct feature *f);
/* Convert string to feature, return pointer after the featurespec. */
char *str2feature(char *str, struct feature *f);

/* Append pattern as feature spec string. */
char *pattern2str(char *str, struct pattern *p);

/* Initialize p and fill it with features matched by the
 * given board move. */
void pattern_match(struct pattern_config *pc, pattern_spec ps, struct pattern *p, struct board *b, struct move *m);


/* Comparative strengths of all feature-payload pairs (initialized to 1 for
 * unspecified pairs). */
struct features_gamma {
	/* Indexed by feature and payload; each feature array is allocated for
	 * all possible payloads to fit in. */
	float *gamma[FEAT_MAX];
	struct pattern_config *pc;
};
/* Default gamma filename to use. */
extern const char *features_gamma_filename;

/* Initializes gamma values, pre-loading existing records from given file
 * (NULL for default), falling back to gamma==1 for unspecified values. */
struct features_gamma *features_gamma_init(struct pattern_config *pc, const char *file);

/* Look up gamma of given feature, or set one if gamma is not NULL. */
static float feature_gamma(struct features_gamma *fg, struct feature *f, float *gamma);


static inline float
feature_gamma(struct features_gamma *fg, struct feature *f, float *gamma)
{
	if (gamma) fg->gamma[f->id][f->payload] = *gamma;
	return fg->gamma[f->id][f->payload];
}

#endif
