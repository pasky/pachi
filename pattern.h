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

/* Each feature is represented by its id and an optional 64-bit payload;
 * when matching, discrete (id,payload) pairs are considered. */

/* This is heavily influenced by (Coulom, 2007), of course. */
/* TODO: Try completely separate ko / no-ko features. */
/* TODO: "Feature set" should encode common parameters - spatial area
 * radius and number of MC-owner playouts. */

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


	/* Unimplemented - TODO: */

	/* Spatial configuration of stones in certain board area. */
	/* XXX: We don't actually care about area size, we simply
	 * incrementally match up to certain radius. */
	/* Payload: Zobrist hash of area */
	FEAT_SPATIAL,

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
	/* Payload: The distance - "line number". */
	FEAT_BORDER,

	/* Last move distance. */
	/* Payload: The distance - gridcular metric (TODO). */
	FEAT_LDIST,

	/* Next-to-last move distance. */
	/* Payload: The distance - gridcular metric (TODO). */
	FEAT_LLDIST,

	/* Monte-carlo owner. */
	/* Payload: #of playouts owning this point at the final
	 * position. All matchers must use the same total amount. */
	FEAT_MCOWNER,
};

struct feature {
	enum feature_id id;
	uint64_t payload;
};

struct pattern {
	/* Pattern (matched) is set of features. */
	int n;
#define FEATURES 32
	struct feature f[FEATURES];
};

/* Append feature to string. */
char *feature2str(char *str, struct feature *f);
/* Convert string to feature, return pointer after the featurespec. */
char *str2feature(char *str, struct feature *f);

/* Initialize p and fill it with features matched by the
 * given board move. */
void pattern_get(struct pattern *p, struct board *b, struct move *m);
/* Append pattern as feature spec string. */
char *pattern2str(char *str, struct pattern *p);

#endif
