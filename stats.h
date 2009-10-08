#ifndef ZZGO_STATS_H
#define ZZGO_STATS_H

/* Move statistics; we track how good value each move has. */

struct move_stats {
	int playouts; // # of playouts
	int wins; // # of BLACK wins
	float value; // wins/playouts
};

/* Add a result to the stats. */
static void stats_add_result(struct move_stats *s, int wins, int playouts);

/* Recompute value based on wins/playouts. */
static void stats_update_value(struct move_stats *s);


static inline void
stats_update_value(struct move_stats *s)
{
	s->value = (float) s->wins / s->playouts;
}

static inline void
stats_add_result(struct move_stats *s, int wins, int playouts)
{
	s->playouts += playouts;
	s->wins += wins;
	stats_update_value(s);
}

#endif
