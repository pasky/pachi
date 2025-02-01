#include <assert.h>
#include <stdio.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "uct/tree.h"
#include "tactics/util.h"
#include "mcowner.h"
#include "criticality.h"


/******************************************************************************************/
/* Point criticality */

void
criticality_init(criticality_t *crit)
{
	memset(crit, 0, sizeof(*crit));
}

/* Collect criticality data after each playout (must be thread-safe). */
void
criticality_collect_data(board_t *start_board, enum stone color,
			 board_t *b, floating_t score, amafmap_t *map, void *data)
{
	criticality_t *crit = (criticality_t*)data;

	floating_t result = (score > 0 ? 1.0 : (score < 0 ? 0.0 : 0.5));
	stats_add_result(&crit->playouts, result, 1);

	/* Record criticality data */
	enum stone winner_color = (result > 0.5 ? S_BLACK : S_WHITE);
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;
		/* Convert board_local_value() to int so we can use atomic builtin (avoids using move_stats).
		 * Value only 0.0, 0.5 or 1.0 so multiply by 2 and divide later on to recover value. */
		int winner_owner = 2 * board_local_value(b, c, winner_color);
		int black_owner  = 2 * board_local_value(b, c, S_BLACK);
		__sync_fetch_and_add(&crit->winner_owner[c], winner_owner);
		__sync_fetch_and_add(&crit->black_owner[c], black_owner);
	} foreach_point_end;
}

/* Compute criticality values after calls to criticality_playout() */
void
criticality_compute(board_t *b, criticality_t *crit)
{
	int games = crit->playouts.playouts;

	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;

		/* Get value, remove factor 2 added in collect_data(). */
		move_stats_t winner_owner = move_stats((float)crit->winner_owner[c] / 2 / games, games);
		move_stats_t black_owner  = move_stats((float)crit->black_owner[c]  / 2 / games, games);
		crit->criticality[c] = point_criticality(&crit->playouts, &winner_owner, &black_owner);
	} foreach_point_end;
}

void
criticality_print_stats(board_t *b, criticality_t *crit)
{
	float criticality_min = 0.0, criticality_max = 0.0;
	coord_t min_coord = pass, max_coord = pass;
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;

		float val = crit->criticality[c];
		if (val > criticality_max) {  criticality_max = val;  max_coord = c;  }
		if (val < criticality_min) {  criticality_min = val;  min_coord = c;  }
	} foreach_point_end;

	char *wr_color = (crit->playouts.value > 0.5 ? "black" : "white");
	float wr       = MAX(crit->playouts.value, 1.0 - crit->playouts.value);
	fprintf(stderr, "criticality:  max: %s %.2f  min: %s %.2f  winrate %s %i%%\n",
		coord2sstr(max_coord), criticality_max,
		coord2sstr(min_coord), criticality_min,
		wr_color, (int)(wr * 100));
}

static void
criticality_printhook(board_t *board, coord_t c, strbuf_t *buf, void *data)
{
        criticality_t *crit = (criticality_t*)data;

	if (c == pass) {
		sbprintf(buf, "Critical area:");
		return;
	}

	const char chr[] = "Oo. ";  // ascii art intensity =)
	const float thres[] = { 0.20, 0.15, 0.10, 0.0 };
	for (int i = 0; i < 4; i++)
		if (crit->criticality[c] >= thres[i]) {
			sbprintf(buf, "%c ", chr[i]);
			return;
		}
        sbprintf(buf, "  ");
}

void
board_print_criticality(board_t *b, FILE *f, criticality_t *crit)
{
        board_print_custom(b, f, criticality_printhook, crit);
}

int
criticality_playout(board_t *b, enum stone color, playout_t *playout,
		    ownermap_t *ownermap, criticality_t *crit)
{
	return batch_playout(b, color, playout, ownermap, false, criticality_collect_data, crit);
}

void
criticality_playouts(int threads, int games, board_t *b, enum stone color,
		     ownermap_t *ownermap, criticality_t *crit)
{
	criticality_init(crit);
	batch_playouts(threads, games, b, color, ownermap, false, criticality_collect_data, crit);
	criticality_compute(b, crit);
}
