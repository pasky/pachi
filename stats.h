#ifndef ZZGO_STATS_H
#define ZZGO_STATS_H

/* Move statistics; we track how good value each move has. */

struct move_stats {
	int playouts; // # of playouts
	float value; // BLACK wins/playouts
};

/* Add a result to the stats. */
static void stats_add_result(struct move_stats *s, float result, int playouts);

/* Merge two stats together. */
static void stats_merge(struct move_stats *dest, struct move_stats *src);

/* Reverse stats parity. */
static void stats_reverse_parity(struct move_stats *s);


static inline void
stats_add_result(struct move_stats *s, float result, int playouts)
{
	s->playouts += playouts;
	s->value += (result - s->value) * playouts / s->playouts;
}

static inline void
stats_merge(struct move_stats *dest, struct move_stats *src)
{
	if (src->playouts > 0)
		stats_add_result(dest, src->value, src->playouts);
}

static inline void
stats_reverse_parity(struct move_stats *s)
{
	s->value = 1 - s->value;
}

#endif
