#ifndef ZZGO_STATS_H
#define ZZGO_STATS_H

/* Move statistics; we track how good value each move has. */

struct move_stats {
	int playouts; // # of playouts
	int wins; // # of BLACK wins
	float value; // wins/playouts
};

/* Add a result to the stats. */
static void stats_add_result(struct move_stats *s, float result, int playouts);

/* Merge two stats together. */
static void stats_merge(struct move_stats *dest, struct move_stats *src);

/* Recompute value based on wins/playouts. */
static void stats_update_value(struct move_stats *s);


static inline void
stats_update_value(struct move_stats *s)
{
	s->value = (float) s->wins / s->playouts;
}

static inline void
stats_add_result(struct move_stats *s, float result, int playouts)
{
	s->playouts += playouts;
	s->wins += result * playouts;
	stats_update_value(s);
}

static inline void
stats_merge(struct move_stats *dest, struct move_stats *src)
{
	dest->playouts += src->playouts;
	dest->wins += src->wins;
	if (likely(dest->playouts))
		stats_update_value(dest);
}

#endif
