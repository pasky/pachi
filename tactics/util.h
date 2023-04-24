#ifndef PACHI_TACTICS_UTIL_H
#define PACHI_TACTICS_UTIL_H

/* Tactical checks and utilities non-essential to the board implementation. */

#include "board.h"
#include "debug.h"

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

/* Tactical evaluation of move @coord by color @color, given
 * simulation end position @b. I.e., a move is tactically good
 * if the resulting group stays on board until the game end.
 * The value is normalized to [0,1]. */
/* We can also take into account surrounding stones, e.g. to
 * encourage taking off external liberties during a semeai. */
static double board_local_value(bool scan_neis, board_t *b, coord_t coord, enum stone color);


static inline int
coord_edge_distance(coord_t c)
{
	int stride = the_board_stride();
	int x = coord_x(c), y = coord_y(c);
	int dx = x > stride / 2 ? stride - 1 - x : x;
	int dy = y > stride / 2 ? stride - 1 - y : y;
	return (dx < dy ? dx : dy) - 1 /* S_OFFBOARD */;
}

static inline int
coord_gridcular_distance(coord_t c1, coord_t c2)
{
	int dx = abs(coord_dx(c1, c2)), dy = abs(coord_dy(c1, c2));
	return dx + dy + (dx > dy ? dx : dy);
}

static inline double
board_local_value(bool scan_neis, board_t *b, coord_t coord, enum stone color)
{
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

#endif
