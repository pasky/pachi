#ifndef ZZGO_PATTERN_H
#define ZZGO_PATTERN_H

/* Matching of multi-featured patterns. */

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


	/* Unimplemented - TODO: */

	/* Spatial configuration of stones in certain board area. */
	/* XXX: We don't actually care about area size, we simply
	 * incrementally match up to certain radius. */
	/* Payload: Zobrist hash of area */
	FEAT_SPATIAL,

	/* This is a pass. */
	/* Payload: [bit0] Last move was also pass? */
#define PF_PASS_LASTPASS	1
	FEAT_PASS,

	/* Simple capture move. */
	/* Payload: [bit0] Capturing laddered group? */
#define PF_CAPTURE_LADDER	1
	/*          [bit1] Re-capturing last move? */
#define PF_CAPTURE_RECAPTURE	2
	/*          [bit2] Enables our atari group get more libs? */
#define PF_CAPTURE_ATARIDEF	4
	FEAT_CAPTURE,

	/* Atari escape (extension). */
	/* Payload: [bit0] Escaping with laddered group? */
#define PF_AESCAPE_LADDER	1
	FEAT_AESCAPE,

	/* Self-atari move. */
	/* Payload: [bit0] Also using our complex definition? */
#define PF_SELFATARI_SMART	1
	FEAT_SELFATARI,

	/* Atari move. */
	/* Payload: [bit0] The atari'd group gets laddered? */
#define PF_ATARI_LADDER		1
	/*          [bit1] Playing ko? */
#define PF_ATARI_KO		2
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

#endif
