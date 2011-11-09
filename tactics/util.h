#ifndef PACHI_TACTICS_UTIL_H
#define PACHI_TACTICS_UTIL_H

/* Advanced tactical checks non-essential to the board implementation. */

#include "board.h"
#include "debug.h"

struct move_queue;

/* Checks if there are any stones in n-vincinity of coord. */
bool board_stone_radar(struct board *b, coord_t coord, int distance);

/* Measure various distances on the board: */
/* Distance from the edge; on edge returns 0. */
static int coord_edge_distance(coord_t c, struct board *b);
/* Distance of two points in gridcular metric - this metric defines
 * circle-like structures on the square grid. */
static int coord_gridcular_distance(coord_t c1, coord_t c2, struct board *b);

/* Construct a "common fate graph" from given coordinate; that is, a weighted
 * graph of intersections where edges between all neighbors have weight 1,
 * but edges between neighbors of same color have weight 0. Thus, this is
 * "stone chain" metric in a sense. */
/* The output are distanes from start stored in given [board_size2()] array;
 * intersections further away than maxdist have all distance maxdist+1 set. */
void cfg_distances(struct board *b, coord_t start, int *distances, int maxdist);

/* Compute an extra komi describing the "effective handicap" black receives
 * (returns 0 for even game with 7.5 komi). @stone_value is value of single
 * handicap stone, 7 is a good default. */
/* This is just an approximation since in reality, handicap seems to be usually
 * non-linear. */
floating_t board_effective_handicap(struct board *b, int first_move_value);

/* Decide if the given player wins counting on the board, considering
 * that given groups are dead. (To get the list of dead groups, use
 * e.g. groups_of_status().) */
bool pass_is_safe(struct board *b, enum stone color, struct move_queue *mq);

/* Returns estimated number of remaining moves for one player until end of game. */
int board_estimated_moves_left(struct board *b);

/* To avoid running out of time, assume we always have at least 30 more moves
 * to play if we don't have more precise information from gtp time_left: */
#define MIN_MOVES_LEFT 30

/* Tactical evaluation of move @coord by color @color, given
 * simulation end position @b. I.e., a move is tactically good
 * if the resulting group stays on board until the game end.
 * The value is normalized to [0,1]. */
/* We can also take into account surrounding stones, e.g. to
 * encourage taking off external liberties during a semeai. */
static double board_local_value(bool scan_neis, struct board *b, coord_t coord, enum stone color);


static inline int
coord_edge_distance(coord_t c, struct board *b)
{
	int x = coord_x(c, b), y = coord_y(c, b);
	int dx = x > board_size(b) / 2 ? board_size(b) - 1 - x : x;
	int dy = y > board_size(b) / 2 ? board_size(b) - 1 - y : y;
	return (dx < dy ? dx : dy) - 1 /* S_OFFBOARD */;
}

static inline int
coord_gridcular_distance(coord_t c1, coord_t c2, struct board *b)
{
	int dx = abs(coord_dx(c1, c2, b)), dy = abs(coord_dy(c1, c2, b));
	return dx + dy + (dx > dy ? dx : dy);
}

static inline double
board_local_value(bool scan_neis, struct board *b, coord_t coord, enum stone color)
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
