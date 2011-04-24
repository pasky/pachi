#ifndef PACHI_STATS_H
#define PACHI_STATS_H

#include <math.h>

/* Move statistics; we track how good value each move has. */
/* These operations are supposed to be atomic - reasonably
 * safe to perform by multiple threads at once on the same stats.
 * What this means in practice is that perhaps the value will get
 * slightly wrong, but not drastically corrupted. */

struct move_stats {
	int playouts; // # of playouts
	floating_t value; // BLACK wins/playouts
};

/* Add a result to the stats. */
static void stats_add_result(struct move_stats *s, floating_t result, int playouts);

/* Remove a result from the stats. */
static void stats_rm_result(struct move_stats *s, floating_t result, int playouts);

/* Merge two stats together. THIS IS NOT ATOMIC! */
static void stats_merge(struct move_stats *dest, struct move_stats *src);

/* Reverse stats parity. */
static void stats_reverse_parity(struct move_stats *s);

/* Temper value based on parent value in specified way - the value should be
 * usable standalone then, representing an improvement against parent value. */
static floating_t stats_temper_value(floating_t val, floating_t pval, int mode);


/* We actually do the atomicity in a pretty hackish way - we simply
 * rely on the fact that int,floating_t operations should be atomic with
 * reasonable compilers (gcc) on reasonable architectures (i386,
 * x86_64). */
/* There is a write order dependency - when we bump the playouts,
 * our value must be already correct, otherwise the node will receive
 * invalid evaluation if that's made in parallel, esp. when
 * current s->playouts is zero. */

static inline void
stats_add_result(struct move_stats *s, floating_t result, int playouts)
{
	int s_playouts = s->playouts;
	floating_t s_value = s->value;
	/* Force the load, another thread can work on the
	 * values in parallel. */
	__sync_synchronize(); /* full memory barrier */

	s_playouts += playouts;
	s_value += (result - s_value) * playouts / s_playouts;

	/* We rely on the fact that these two assignments are atomic. */
	s->value = s_value;
	__sync_synchronize(); /* full memory barrier */
	s->playouts = s_playouts;
}

static inline void
stats_rm_result(struct move_stats *s, floating_t result, int playouts)
{
	if (s->playouts > playouts) {
		int s_playouts = s->playouts;
		floating_t s_value = s->value;
		/* Force the load, another thread can work on the
		 * values in parallel. */
		__sync_synchronize(); /* full memory barrier */

		s_playouts -= playouts;
		s_value += (s_value - result) * playouts / s_playouts;

		/* We rely on the fact that these two assignments are atomic. */
		s->value = s_value;
		__sync_synchronize(); /* full memory barrier */
		s->playouts = s_playouts;

	} else {
		/* We don't touch the value, since in parallel, another
		 * thread can be adding a result, thus raising the
		 * playouts count after we zero the value. Instead,
		 * leaving the value as is with zero playouts should
		 * not break anything. */
		s->playouts = 0;
	}
}

static inline void
stats_merge(struct move_stats *dest, struct move_stats *src)
{
	/* In a sense, this is non-atomic version of stats_add_result(). */
	if (src->playouts) {
		dest->playouts += src->playouts;
		dest->value += (src->value - dest->value) * src->playouts / dest->playouts;
	}
}

static inline void
stats_reverse_parity(struct move_stats *s)
{
	s->value = 1 - s->value;
}

static inline floating_t
stats_temper_value(floating_t val, floating_t pval, int mode)
{
	floating_t tval = val;
	floating_t expd = val - pval;
	switch (mode) {
		case 1: /* no tempering */
			tval = val;
			break;
		case 2: /* 0.5+(result-expected)/2 */
			tval = 0.5 + expd / 2;
			break;
		case 3: { /* 0.5+bzz((result-expected)^2) */
			floating_t ntval = expd * expd;
			/* val = 1 pval = 0.8 : ntval = 0.04 tval = 0.54
			 * val = 1 pval = 0.6 : ntval = 0.16 tval = 0.66
			 * val = 1 pval = 0.3 : ntval = 0.49 tval = 0.99
			 * val = 1 pval = 0.1 : ntval = 0.81 tval = 1.31 */
			tval = 0.5 + (val > 0.5 ? 1 : -1) * ntval;
			break; }
		case 4: /* 0.5+sqrt(result-expected)/2 */
			tval = 0.5 + copysignf(sqrt(fabs(expd)), expd) / 2;
			break;
		default: assert(0); break;
	}
	return tval;
}

#endif
