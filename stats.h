#ifndef ZZGO_STATS_H
#define ZZGO_STATS_H

/* Move statistics; we track how good value each move has. */

struct move_stats {
	int playouts; // # of playouts
	int wins; // # of BLACK wins
	float value; // wins/playouts
};

#endif
