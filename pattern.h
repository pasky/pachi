#ifndef ZZGO_PATTERN_H
#define ZZGO_PATTERN_H

/* Matching of multi-featured patterns. */

/* When someone says "pattern", you imagine a configuration of stones in given
 * area (e.g. as matched very efficiently by pattern3 in case of 3x3 area).
 * However, we use a richer definition of pattern, where this is merely one
 * pattern _feature_. Another features may be is-a-selfatari, is-a-capture,
 * number of liberties, distance from last move, etc. */

/* Each feature is represented by its id and an optional 64-bit payload. */

enum feature_id {
	/* Implemented */

	/* Unimplemented - TODO */

	/* Spatial configuration of stones in certain board area. */
	/* XXX: We don't actually care about area size, we simply
	 * incrementally match up to certain radius. */
	/* Payload: Zobrist hash of area */
	FEAT_SPATIAL,

	FEAT_CAPTURE,		/* */
};

struct feature {
	enum feature_id id;
	uint64_t payload;
};

#endif
