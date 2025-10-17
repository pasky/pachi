#include <assert.h>
#include <stdio.h>
#include <limits.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "uct/tree.h"
#include "tactics/util.h"
#include "mcowner.h"
#include "criticality.h"

/* Move criticality */
#include "uct/tree.h"
#include "tactics/util.h"
#include "tactics/selfatari.h"


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



/******************************************************************************************/
/* Move criticality  (first-play criticality) */


void
move_criticality_init(move_criticality_t *crit, board_t *b, enum stone color)
{
	memset(crit, 0, sizeof(*crit));
	crit->color = color;

	/* Init consider map */
	foreach_point(b) {
		if (board_at(b, c) == S_NONE &&
		    board_is_valid_play_no_suicide(b, color, c)) {
			crit->consider[c] = 1;

			if (!board_playing_ko_threat(b) && is_selfatari(b, color, c))
				crit->is_selfatari[c] = 1;
		}
	} foreach_point_end;
}

/* We want to know how correlated "playing first at c" and "winning the game" are.
 *    move_criticality(player) = cov(pl_play_first, pl_wins)
 *                             = pl_play_first_pl_wins - pl_play_first * pl_wins
 */
static float
move_criticality(move_criticality_t *crit, coord_t c)
{
	int games = crit->playouts.playouts;
	float pl_play_first_pl_wins = (float)crit->play_first_wins[c] / games;
	float pl_play_first = (float)crit->play_first[c] / games;
	float pl_wins = (crit->color == S_BLACK ? crit->playouts.value : 1.0 - crit->playouts.value);
	return (pl_play_first_pl_wins - pl_play_first * pl_wins);
}

/* Collect criticality data after each playout (must be thread-safe). */
void
move_criticality_collect_data(board_t *b, enum stone color,
			      board_t *final_board, floating_t score, amafmap_t *map, void *data)
{
	move_criticality_t *crit = (move_criticality_t*)data;

	floating_t result = (score > 0 ? 1.0 : (score < 0 ? 0.0 : 0.5));
	enum stone winner_color = (score > 0 ? S_BLACK : S_WHITE);
	stats_add_result(&crit->playouts, result, 1);

	/* Find first play at each location. */
	first_play_t fp;
	int *first_move = amaf_first_play(map, b, &fp);

	int start = map->game_baselen;
	foreach_point(b) {
		if (!crit->consider[c])
			continue;

		int first = first_move[c];
		if (first == INT_MAX)  continue;

		/* Move is only used if it was first played by the same color. */
		int distance = first - start;
		if (distance & 1)
			continue;

		/* Exclude selfatari moves which were not selfataris when played
		 * (typically a case of amaf logic confusing cause and effect). */
		if (crit->is_selfatari[c] && !amaf_is_selfatari(map, first))
			continue;

		/* Ignore last moves, matching moves at the very end leads to weird
		 * artefacts like thinking a dame in an uncertain area is super
		 * critical while actually playing there is just a sign the group
		 * was captured / survived. Keep as much as possible though, some
		 * life and death moves get played very late. */
		if (distance > (map->gamelen - map->game_baselen) * 90/100)
			continue;

		__sync_fetch_and_add(&crit->play_first[c], 1);
		if (winner_color == color)
			__sync_fetch_and_add(&crit->play_first_wins[c], 1);
	} foreach_point_end;
}

/* Compute move criticality values after calls to move_criticality_playout() */
void
move_criticality_compute(board_t *b, move_criticality_t *crit, float bottom_moves_filter)
{
	/* Find most played move. */
	int playouts_max = 0;
	foreach_point(b) {
		if (!crit->consider[c])
			continue;
		playouts_max = MAX(playouts_max, crit->play_first[c]);
	} foreach_point_end;

	foreach_point(b) {
		if (!crit->consider[c])
			continue;

		/* Filter out rarely played moves below filter. */
		if (crit->play_first[c] < playouts_max * bottom_moves_filter)
			continue;

		crit->criticality[c] = move_criticality(crit, c);
	} foreach_point_end;
}

void
move_criticality_print_stats(board_t *b, move_criticality_t *crit)
{
	float criticality_min = 0.0, criticality_max = 0.0;
	coord_t min_coord = pass, max_coord = pass;
	foreach_point(b) {
		if (!crit->consider[c])
			continue;

		float val = crit->criticality[c];
		if (val > criticality_max) {  criticality_max = val;  max_coord = c;  }
		if (val < criticality_min) {  criticality_min = val;  min_coord = c;  }
	} foreach_point_end;

	char *wr_color = (crit->playouts.value > 0.5 ? "black" : "white");
	float wr       = MAX(crit->playouts.value, 1.0 - crit->playouts.value);
	fprintf(stderr, "move criticality:  max: %s %.2f  min: %s %.2f  winrate %s %i%%\n",
		coord2sstr(max_coord), criticality_max,
		coord2sstr(min_coord), criticality_min,
		wr_color, (int)(wr * 100));
}

int
move_criticality_playout(board_t *b, enum stone color, playout_t *playout,
			 ownermap_t *ownermap, move_criticality_t *crit)
{
	return batch_playout(b, color, playout, ownermap, true, move_criticality_collect_data, crit);
}

void
move_criticality_playouts(int threads, int games, board_t *b, enum stone color,
			  ownermap_t *ownermap, move_criticality_t *crit,
			  float bottom_moves_filter)
{
	move_criticality_init(crit, b, color);
	batch_playouts(threads, games, b, color, ownermap, true, move_criticality_collect_data, crit);
	move_criticality_compute(b, crit, bottom_moves_filter);
}



/******************************************************************************************/
/* AMAF criticality */


void
amaf_criticality_init(amaf_criticality_t *r, board_t *b, enum stone color)
{
	memset(r, 0, sizeof(*r));
	r->color = color;

	/* Init consider map */
	foreach_point(b) {
		if (board_at(b, c) == S_NONE &&
		    board_is_valid_play_no_suicide(b, color, c)) {
			r->consider[c] = 1;

			if (!board_playing_ko_threat(b) && is_selfatari(b, color, c))
				r->is_selfatari[c] = 1;
		}
	} foreach_point_end;
}

/* Collect criticality data after each playout (must be thread-safe).
 * Replicate ucb1amaf_update() logic with flat playouts. */
void
amaf_criticality_collect_data(board_t *b, enum stone color,
			      board_t *final_board, floating_t score, amafmap_t *map, void *data)
{
	amaf_criticality_t *r = (amaf_criticality_t*)data;
	assert(r->color == color);

	floating_t result = (score > 0 ? 1.0 : (score < 0 ? 0.0 : 0.5));
	stats_add_result(&r->playouts, result, 1);

	/* Find first play at each location. */
	first_play_t fp;
	int *first_move = amaf_first_play(map, b, &fp);

	int start = map->game_baselen;		/* Start of amaf range */
	int max_threat_dist = amaf_ko_length(map, start);

	foreach_point(b) {
		if (!r->consider[c])
			continue;
		int first = first_move[c];
		if (first == INT_MAX)  continue;
		assert(first >= start && first < map->gamelen);

		/* Move is only used if it was first played by the same color. */
		int distance = first - start;
		if (distance & 1)
			continue;

		/* Exclude selfatari moves which were not selfataris when played
		 * (typically a case of amaf logic confusing cause and effect). */
		if (r->is_selfatari[c] && !amaf_is_selfatari(map, first))
			continue;

		int weight = 1;
		floating_t res = result;   // from black perspective

		/* Don't give amaf bonus to a ko threat before taking the ko.
		 * http://www.grappa.univ-lille3.fr/~coulom/Aja_PhD_Thesis.pdf */
		if (distance <= max_threat_dist && distance % 6 == 4)
			weight = 0;
		else {
			/* Give more weight to moves played earlier.
			 * With distance_rave = 3 we get a boost of 2, 1 or 0 for endgame moves.
			 * Final weight = 3, 2 or 1 */
			weight += 3 * (map->gamelen - first) / (map->gamelen - start + 1);
		}

		if (weight)
			stats_add_result(&r->amaf[c], res, weight);
	} foreach_point_end;
}

/* Compute amaf criticality from collected data.
 * @b must be initial board */
void
amaf_criticality_compute(board_t *b, amaf_criticality_t *r, float bottom_moves_filter)
{
	memset(r->rating, 0, sizeof(r->rating));
	assert(is_player_color(r->color));

	/* Find playouts_max first */
	int playouts_max = 0;
	foreach_point(b) {
		if (!r->consider[c])
			continue;
		playouts_max = MAX(playouts_max, r->amaf[c].playouts);
	} foreach_point_end;

	/* Ratings */
	foreach_point(b) {
		if (!r->consider[c])
			continue;

		/* Filter out rarely played moves below filter. */
		if (r->amaf[c].playouts < playouts_max * bottom_moves_filter)
			continue;

		/* move rating = winrate diff 
		 *             = move amaf winrate - global winrate */
		float diff = r->amaf[c].value - r->playouts.value;
		if (r->color == S_WHITE)  // amaf.value is from b perspective
			diff = -diff;
		r->rating[c] = diff;
	} foreach_point_end;
}

void
amaf_criticality_print_stats(board_t *b, amaf_criticality_t *r)
{
	float   crit_min = 0.0,   crit_max = 0.0;
	coord_t min_coord = pass, max_coord = pass;
	foreach_point(b) {
		float val = r->rating[c];
		if (val > crit_max) {  crit_max = val;  max_coord = c;  }
		if (val < crit_min) {  crit_min = val;  min_coord = c;  }
	} foreach_point_end;

	char *wr_color = (r->playouts.value > 0.5 ? "black" : "white");
	float wr       = MAX(r->playouts.value, 1.0 - r->playouts.value);
	fprintf(stderr, "amaf criticality:  max: %s %.2f  min: %s %.2f  winrate %s %i%%\n",
		coord2sstr(max_coord), crit_max,
		coord2sstr(min_coord), crit_min,
		wr_color, (int)(wr * 100));
}

int
amaf_criticality_playout(board_t *b, enum stone color, playout_t *playout,
			 ownermap_t *ownermap, amaf_criticality_t *crit)
{
	return batch_playout(b, color, playout, ownermap, true, amaf_criticality_collect_data, crit);
}

void
amaf_criticality_playouts(int threads, int games, board_t *b, enum stone color,
			  ownermap_t *ownermap, amaf_criticality_t *crit,
			  float bottom_moves_filter)
{
	amaf_criticality_init(crit, b, color);
	batch_playouts(threads, games, b, color, ownermap, true, amaf_criticality_collect_data, crit);
	amaf_criticality_compute(b, crit, bottom_moves_filter);
}
