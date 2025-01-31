#ifndef PACHI_PATTERN_MCOWNER_H
#define PACHI_PATTERN_MCOWNER_H

/* Infrastructure to run single or multi-threaded batch playouts to collect data like
 * ownermap or criticality outside of tree search.
 * Caller can provide callback to collect data at the end of each playout. */


#include "board.h"
#include "ownermap.h"
#include "playout.h"

typedef struct playout_setup playout_setup_t;
typedef struct playout_policy playout_policy_t;
typedef struct pattern_config pattern_config_t;

/* Callback for collecting data at the end of each playout.
 * @score is score from black's perspective.
 * @data can be used to pass data structure to store results.
 * Handling of shared data structure must be thread-safe as it can be called concurrently. */
typedef void (*collect_data_t)(board_t *start_board, enum stone color,
			       board_t *final_board, floating_t score,
			       amafmap_t *map, void *data);


/* Batch playouts */

#define MAX_THREADS	(get_nprocessors())

/* Run a batch of single/multi-threaded playouts and collect data.
 * Caller can provide callback to collect data in addition to @ownermap.
 * @amafmap_needed indicates whether to collect amaf data. */
void batch_playouts(int threads, int games, board_t *b, enum stone color,
		    ownermap_t *ownermap, bool amafmap_needed,
		    collect_data_t collect_data, void *data);

/* Copy board, play single game and collect data.
 * @collect_data callback is called at the end if present.
 * @ownermap, @amafmap_needed: whether to collect ownership/amaf data. */
int batch_playout(board_t *board, enum stone color, playout_t *playout,
		  ownermap_t *ownermap, bool amafmap_needed,
		  collect_data_t collect_data, void *data);


/* MCowner playouts */

/* Play games and fill ownermap. */
void mcowner_playouts(int threads, int games, board_t *b, enum stone color, ownermap_t *ownermap);

/* For hacking purposes, fast inaccurate version. */
void mcowner_playouts_fast(board_t *b, enum stone color, ownermap_t *ownermap);


#endif
