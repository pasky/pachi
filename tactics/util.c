#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "tactics/util.h"


bool
board_stone_radar(board_t *b, coord_t coord, int distance)
{
	int cx = coord_x(coord), cy = coord_y(coord);
	int bounds[4] = { cx - distance, cy - distance,
			  cx + distance, cy + distance  };
	for (int i = 0; i < 4; i++)
		if (bounds[i] < 1)
			bounds[i] = 1;
		else if (bounds[i] > board_rsize(b))
			bounds[i] = board_rsize(b);
	for (int x = bounds[0]; x <= bounds[2]; x++)
		for (int y = bounds[1]; y <= bounds[3]; y++) {
			coord_t c = coord_xy(x, y);
			if (c != coord && board_at(b, c) != S_NONE) {
				/* fprintf(stderr, "radar %d,%d,%d: %d,%d (%d)\n",
					coord_x(coord), coord_y(coord),
					distance, x, y, board_atxy(x, y)); */
				return true;
			}
		}
	return false;
}

float
coord_distance(coord_t c1, coord_t c2)
{
	int dx = coord_dx(c1, c2),  dy = coord_dy(c1, c2);
	return sqrtf(dx * dx + dy * dy);
}

void
cfg_distances(board_t *b, coord_t start, int *distances, int maxdist)
{
	/* Queue for d+1 spots; no two spots of the same group
	 * should appear in the queue. */
#define qinc(x) (x = ((x + 1) >= board_max_coords(b) ? ((x) + 1 - board_max_coords(b)) : (x) + 1))
	coord_t queue[board_max_coords(b)]; int qstart = 0, qstop = 0;

	foreach_point(b) {
		distances[c] = board_at(b, c) == S_OFFBOARD ? maxdist + 1 : -1;
	} foreach_point_end;

	queue[qstop++] = start;
	for (int d = 0; d <= maxdist; d++) {
		/* Process queued moves, while setting the queue
		 * for new wave. */
		int qa = qstart, qb = qstop;
		qstart = qstop;
		for (int q = qa; q < qb; qinc(q)) {
#define cfg_one(coord, grp) do {\
	distances[coord] = d; \
	foreach_neighbor (b, coord, { \
		if (distances[c] < 0 && (!grp || group_at(b, coord) != grp)) { \
			queue[qstop] = c; \
			qinc(qstop); \
		} \
	}); \
} while (0)
			coord_t cq = queue[q];
			if (distances[cq] >= 0)
				continue; /* We already looked here. */
			if (board_at(b, cq) == S_NONE) {
				cfg_one(cq, 0);
			} else {
				group_t g = group_at(b, cq);
				foreach_in_group(b, g) {
					cfg_one(c, g);
				} foreach_in_group_end;
			}
#undef cfg_one
		}
	}

	foreach_point(b) {
		if (distances[c] < 0)
			distances[c] = maxdist + 1;
	} foreach_point_end;
}


floating_t
board_effective_handicap(board_t *b, int first_move_value)
{
	/* This can happen if the opponent passes during handicap
	 * placing phase. */
	// assert(b->handicap != 1);

	/* Always return 0 for even games, in particular if
	 * first_move_value is set on purpose to a value different
	 * from the correct theoretical value (2*komi). */
	if (!b->handicap)
		return b->komi == 0.5 ? 0.5 * first_move_value : 7.5 - b->komi;
	return b->handicap * first_move_value + 0.5 - b->komi;
}


/* On average 20% of points remain empty at the end of a game */
#define EXPECTED_FINAL_EMPTY_PERCENT 20

/* Returns estimated number of remaining moves for one player until end of game. */
int
board_estimated_moves_left(board_t *b)
{
	int total_points = (board_rsize(b)) * (board_rsize(b));
	int moves_left = (b->flen - total_points*EXPECTED_FINAL_EMPTY_PERCENT/100)/2;
	return moves_left > MIN_MOVES_LEFT ? moves_left : MIN_MOVES_LEFT;
}


/***********************************************************************************************/
/* board quadrants */

/* Coord board quadrant:
      +---------------------------+
   13 | 0 0 0 0 0 0 0 1 1 1 1 1 1 |
   12 | 0 0 0 0 0 0 0 1 1 1 1 1 1 |
   11 | 0 0 0 0 0 0 0 1 1 1 1 1 1 |
   10 | 0 0 0 0 0 0 0 1 1 1 1 1 1 |
    9 | 0 0 0 0 0 0 0 1 1 1 1 1 1 |
    8 | 0 0 0 0 0 0 0 1 1 1 1 1 1 |
    7 | 3 3 3 3 3 3 0 1 1 1 1 1 1 |
    6 | 3 3 3 3 3 3 2 2 2 2 2 2 2 |
    5 | 3 3 3 3 3 3 2 2 2 2 2 2 2 |
    4 | 3 3 3 3 3 3 2 2 2 2 2 2 2 |
    3 | 3 3 3 3 3 3 2 2 2 2 2 2 2 |
    2 | 3 3 3 3 3 3 2 2 2 2 2 2 2 |
    1 | 3 3 3 3 3 3 2 2 2 2 2 2 2 |
      +---------------------------+   
        A B C D E F G H J K L M N      */
int
coord_quadrant(coord_t c)
{
	if (is_pass(c))
		return 0;
	
	/* Multiply everything by 2 so works for even-sized boards too */
	int x = coord_x(c) * 2;			// x
	int y = coord_y(c) * 2;			// y
	int mid = the_board_rsize() + 1;	// (board_size + 1) / 2
	
	if (y > mid)  return (x <= mid ? 0 : 1);
	if (y < mid)  return (x >= mid ? 2 : 3);

	if (x < mid)  return 3;
	if (x > mid)  return 1;
	return 0;	/* Tengen */
}

/* Return opposite quadrant (diagonal) */
int
diag_quadrant(int quad)
{
	assert(quad >= 0 && quad <= 3);
	
	static int diag[] = { 2, 3, 0, 1 };
	return diag[quad];
}

int
rotate_quadrant(int q, int rot)
{
	static int rotx[4] = { 1, 0, 3, 2 };
	static int roty[4] = { 3, 2, 1, 0 };
	static int rotr[4] = { 3, 0, 1, 2 };

	if (rot & 1)  q = roty[q];
	if (rot & 2)  q = rotx[q];
	if (rot & 4)  q = rotr[q];
	return q;
}


/***********************************************************************************************/
/* fuseki */

/* Number of high fuseki stones for color @color */
int
fuseki_high_stones(board_t *b, enum stone color)
{
	int high_own = 0;
	
	foreach_point(b) {
		if (board_at(b, c) == color && coord_edge_distance(c) >= 3)
			high_own++;
	} foreach_point_end;
	
	return high_own;
}

int
fuseki_high_stones_by_quadrant(board_t *b, enum stone color, int quad)
{
	int high_own = 0;
	
	foreach_point(b) {
		if (board_at(b, c) == color &&
		    coord_quadrant(c) == quad &&
		    coord_edge_distance(c) >= 3)
			high_own++;
	} foreach_point_end;
	
	return high_own;
}

/* Return sum of edge distances for first @n moves of color @color.
 * Take captured stones' moves into account:
 * use move history, not current board stones */
static int
fuseki_stone_heights_sum(board_t *b, enum stone color, int n)	
{
	move_history_t *hist = b->move_history;
	int heights = 0;

	for (int i = 0; i < n; i++) {
		move_t *m = &hist->move[i];
		if (m->color == color && !is_pass(m->coord))
			heights += coord_edge_distance(m->coord);
	}
	
	return heights;
}

/* Stone height difference between opponent stones and ours.
 * Always compare equal number of stones (ignore last move if necessary).
 * Only makes sense for even games. */
int
fuseki_stone_heights_diff(board_t *b, enum stone color)
{
	int n = b->move_history->moves & ~0x1;  /* ignore last move if necessary */
	int heights_own   = fuseki_stone_heights_sum(b, color, n);
	int heights_other = fuseki_stone_heights_sum(b, stone_other(color), n);
	return (heights_other - heights_own);
}

bool
playing_against_influence_fuseki(board_t *b)
{
	enum stone color = board_to_play(b);
	
	if (b->handicap || b->moves < 10 || b->moves > 65)
		return false;

#if 0
	/* Simple but lots of false positive ... */
	int high_own   = fuseki_high_stones(b, color);
	int high_other = fuseki_high_stones(b, stone_other(color));
	return (high_other >= 10 && high_other >= 2 * high_own - 2);
#endif

	int diff = fuseki_stone_heights_diff(b, color);
	int quadrants = 0;
	for (int q = 0; q < 4; q++)
		if (fuseki_high_stones_by_quadrant(b, stone_other(color), q) >= 4)
			quadrants++;
	
	return (diff >= 10 && quadrants >= 3);
}
