#ifndef PACHI_TACTICS_UTIL_H
#define PACHI_TACTICS_UTIL_H

/* Tactical checks and utilities non-essential to the board implementation. */

#include "board.h"
#include "debug.h"
#include "stats.h"

/* Checks if there are any stones in n-vincinity of coord. */
bool board_stone_radar(board_t *b, coord_t coord, int distance);

/* Measure various distances on the board: */
/* Distance from the edge; on edge returns 0. */
static int coord_edge_distance(coord_t c);
/* Distance of two points in gridcular metric - this metric defines
 * circle-like structures on the square grid. */
static int coord_gridcular_distance(coord_t c1, coord_t c2);
/* Regular distance */
float coord_distance(coord_t c1, coord_t c2);

/* returns coord board quadrant:
 *   [ 0 1 ]   or -1 if on center lines
 *   [ 3 2 ]   */
int coord_quadrant(coord_t c);
/* last move quadrant */
#define last_quadrant(b)		( coord_quadrant(last_move(b).coord) )
/* return opposite quadrant (diagonal) */
int diag_quadrant(int quad);
/* Rotate quadrant according to rot: [0-7] for 8 board symmetries.
 * Bad idea to use this most of the time as:
 *     rotate_quadrant(coord_quadrant(c), rot) == coord_quadrant(rotate_coord(c, rot))
 * doesn't hold for center lines. Instead use:
 *     coord_quadrant(rotate_coord(c, rot))  */
int rotate_quadrant(int q, int rot);
/* Check if coord is on or close to quadrant boundary. */
bool near_ambiguous_quadrant_coord(coord_t c);
/* Ambiguous last coord quadrant ? */
#define near_ambiguous_last_quadrant(b) (near_ambiguous_quadrant_coord(last_move(b).coord))

/* Cona_t "common fate graph" from given coordinate; that is, a weighted
 * graph of intersections where edges between all neighbors have weight 1,
 * but edges between neighbors of same color have weight 0. Thus, this is
 * "stone chain" metric in a sense. */
/* The output are distances from start stored in given [board_max_coords()] array;
 * intersections further away than maxdist have all distance maxdist+1 set. */
void cfg_distances(board_t *b, coord_t start, int *distances, int maxdist);

/* Compute an extra komi describing the "effective handicap" black receives
 * (returns 0 for even game with 7.5 komi). @stone_value is value of single
 * handicap stone, 7 is a good default. */
/* This is just an approximation since in reality, handicap seems to be usually
 * non-linear. */
floating_t board_effective_handicap(board_t *b, int first_move_value);

/* Returns estimated number of remaining moves for one player until end of game. */
int board_estimated_moves_left(board_t *b);

/* To avoid running out of time, assume we always have at least 30 more moves
 * to play if we don't have more precise information from gtp time_left: */
#define MIN_MOVES_LEFT 30

/* Number of high fuseki stones for color @color */
int fuseki_high_stones(board_t *b, enum stone color);
int fuseki_high_stones_by_quadrant(board_t *b, enum stone color, int q);
int fuseki_stone_heights_diff(board_t *b, enum stone color);
bool playing_against_influence_fuseki(board_t *b);

/* Evaluate value of point for color @color at playout end given by @b:
 *   1.0 = point controlled by color
 *   0.0 = point controlled by opponent
 * Note that unlike move_local_value(false) which is used by ucb1amaf
 * an eye of the right color counts as 1.0. */
static float board_local_value(board_t *b, coord_t coord, enum stone color);

/* Tactical evaluation of move @coord by color @color, given
 * simulation end position @b. I.e., a move is tactically good
 * if the resulting group stays on board until the game end.
 * The value is normalized to [0,1]. */
/* We can also take into account surrounding stones, e.g. to
 * encourage taking off external liberties during a semeai. */
static double rave_board_local_value(bool scan_neis, board_t *b, coord_t coord, enum stone color);

/* Point criticality  (ownership criticality)
 * Measure how owning the point at the end of playouts and winning the game are correlated. */
static floating_t point_criticality(move_stats_t *playouts, move_stats_t *winner_owner, move_stats_t *black_owner);


static inline int
coord_edge_distance(coord_t c)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(c));
#endif
	int stride = the_board_stride();
	int x = coord_x(c), y = coord_y(c);
	int dx = x > stride / 2 ? stride - 1 - x : x;
	int dy = y > stride / 2 ? stride - 1 - y : y;
	return (dx < dy ? dx : dy) - 1 /* S_OFFBOARD */;
}

static inline int
coord_gridcular_distance(coord_t c1, coord_t c2)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(c1));
	assert(sane_coord(c2));
#endif	
	int dx = abs(coord_dx(c1, c2)), dy = abs(coord_dy(c1, c2));
	return dx + dy + (dx > dy ? dx : dy);
}

static inline float
board_local_value(board_t *b, coord_t c, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(c));
	assert(is_player_color(color));
#endif
	if (board_at(b, c != S_NONE))
		return (board_at(b, c) == color ? 1.0 : 0.0);

	if (board_is_eyelike(b, c, color))
		return 1.0;
	if (board_is_eyelike(b, c, stone_other(color)))
		return 0.0;

	return 0.5;
}

static inline double
rave_board_local_value(bool scan_neis, board_t *b, coord_t coord, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(coord));
	assert(is_player_color(color));
#endif
	if (scan_neis) {
		/* Count surrounding friendly stones and our eyes. */
		int friends = 0;
		foreach_neighbor(b, coord, {
			friends += board_at(b, c) == color || board_at(b, c) == S_OFFBOARD || board_is_one_point_eye(b, c, color);
		});
		return (double) (2 * (board_at(b, coord) == color) + friends) / 6.f;
	} else {
		return (board_at(b, coord) == color) ? 1.f : 0.f;
	}
}

/* The argument: If 'gets' and 'wins' is uncorrelated, b_gets * b_wins is valid way to obtain
 * winner_gets. The more correlated it is, the more distorted the result.
 *
 * point criticality = cov(player_gets, player_wins)
 *                   = player_gets_player_wins - player_gets * player_wins
 *                   = winner_gets - (b_gets * b_wins + w_gets * w_wins)
 *                   = winner_gets - (b_gets * b_wins + (1 - b_gets) * (1 - b_wins))
 *                   = winner_gets - (b_gets * b_wins + 1 - b_gets - b_wins + b_gets * b_wins)
 *                   = winner_gets - (2 * b_gets * b_wins - b_gets - b_wins + 1) */
static inline floating_t
point_criticality(move_stats_t *playouts, move_stats_t *winner_owner, move_stats_t *black_owner)
{
	return (winner_owner->value - (2 * black_owner->value * playouts->value - black_owner->value - playouts->value + 1));
}


#endif
