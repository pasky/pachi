#ifndef PACHI_PATTERN_CRITICALITY_H
#define PACHI_PATTERN_CRITICALITY_H

#include "stats.h"

/* Functions for collecting criticality data in batch playouts.
 * This is mainly for visualization and hacking purposes, mcts doesn't use this code. */


/**************************************************************************************************/
/* Point criticality:   (ownership criticality)
 *
 * Measure how owning a point, and winning the game are correlated in playouts.
 * This is the criticality referred to in research litterature (Coulom 2009, Pellegrino 2009,
 * Baudis 2011). It gives an idea of how important controlling a point is for winning the playout,
 * and can help identify critical areas, but is not necessarily a good move indicator (getting the
 * critical point may be more an effect than a cause of winning the game).
 *
 * What we do here differs slightly from the criticality heuristic in ucb1amaf:
 * - flat playouts, no tree search dynamics
 * - we use board_local_value() for ownership, ucb1amaf uses move_local_value().
 *   move_local_value() does not consider eyes as owned (in an attempt to boost moves that stay on
 *   the board vs moves that get captured and reinvaded later).  */

typedef struct {
	move_stats_t playouts;
	int          winner_owner[BOARD_MAX_COORDS];
	int          black_owner[BOARD_MAX_COORDS];
	float	     criticality[BOARD_MAX_COORDS];
} criticality_t;


/* Criticality playouts */

/* Play games and fill ownermap, criticality. */
void criticality_playouts(int threads, int games, board_t *b, enum stone color, ownermap_t *ownermap, criticality_t *crit);

/* Play single game and collect ownermap, criticality data. 
 * mcowner_compute_criticality() must be called after batch run. */
int criticality_playout(board_t *b, enum stone color, playout_t *playout, ownermap_t *ownermap, criticality_t *crit);


/* Criticality */

void criticality_init(criticality_t *crit);

/* Data collect callback for batch_playout() */
void criticality_collect_data(board_t *start_board, enum stone color,
			      board_t *final_board, floating_t score, amafmap_t *map, void *data);

/* Compute criticality values after calls to criticality_playout(),
 * not needed if using criticality_playouts(). */
void criticality_compute(board_t *b, criticality_t *crit);

void criticality_print_stats(board_t *b, criticality_t *crit);

/* Print criticality map.
 * For debugging, otherwise gogui analyze command is better (colors). */
void board_print_criticality(board_t *b, FILE *f, criticality_t *crit);

/*         A B C D E F G H J K L M N O P Q R S T        A B C D E F G H J K L M N O P Q R S T  
 *       +---------------------------------------+    +---------------------------------------+
 *    19 | . O X X X X X X O O O . X . . . . . . | 19 |                                       |
 *    18 | O . O O X . O O X O . O O X O . X . . | 18 |                                       |
 *    17 | O . O O X X X X X X O X X . . O X . . | 17 |                                       |
 *    16 | . O O X X O O O O X O X . X O X . . . | 16 |                                       |
 *    15 | O O X O O . O O X X O O X . X X . . . | 15 |                                       |
 *    14 | . X X . . O X O O O O O X . . . . . . | 14 |                                       |
 *    13 | . . . X . . X O X O X X O . . . . X . | 13 |                         .             |
 *    12 | . . O X . . X X X X . . . . . . O . . | 12 |                         . . . . . .   |
 *    11 | . O O X . . . . . . X . . . . . X . . | 11 |                         . . . .       |
 *    10 | . O X X . . . . . . . . . . . O X . . | 10 |                       . . . . o   .   |
 *     9 | . X O . . . . . . . . . . . . . O X . |  9 |             . . . . . . .     . .     |
 *     8 | . . O . . . . . . . . . . X O . O X . |  8 |               . . . . . .       .     |
 *     7 | . . . . . . . . O O . . . X X X X O . |  7 |               . . . . . .         .   |
 *     6 | . . . . . O O . X O . O O O O X . O . |  6 |               o O . o . . . .     . . |
 *     5 | . O O O . . X . X X . O X X X O O . O |  5 |           . O O O O O .       . . . . |
 *     4 | . O X X O O X . O . X X O X . X O O . |  4 |             O O O O O O O       . . . |
 *     3 | X X . X O X O O X X . . O X X X X O . |  3 |             O O O O O O O         .   |
 *     2 | . O X X X X . O X O O . O O X . X X O |  2 |             . O O O O O O O           |
 *     1 | . . . . . . . . O). . . . . . . . . . |  1 |             . O O O O O O o .         |
 *       +---------------------------------------+    +---------------------------------------+   */



#endif
