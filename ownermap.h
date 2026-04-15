#ifndef PACHI_OWNERMAP_H
#define PACHI_OWNERMAP_H

/* Map of board intersection owners, and devices to derive group status
 * information from the map. */

#include <signal.h> // sig_atomic_t
#include "stats.h"
#include "mq.h"

/* How many games to consider at minimum before judging groups. */
#define GJ_MINGAMES	500

/* How big proportion of ownermap counts must be of one color to consider
 * the point sure. */
#define GJ_THRES	0.8

enum point_judgement {
	PJ_SEKI = S_NONE,
	PJ_BLACK = S_BLACK,
	PJ_WHITE = S_WHITE,
	PJ_UNKNOWN = 3,
};

enum gj_state {
	GS_NONE,
	GS_DEAD,
	GS_ALIVE,
	GS_UNKNOWN,
};

typedef struct {
	floating_t thres;
	enum gj_state *gs; // [bsize2]
} group_judgement_t;

/* Map of final owners of all intersections on the board.
 * This may be shared between multiple threads!
 * XXX  We assume sig_atomic_t is thread-atomic. This may not be true in pathological cases.
 * TODO We may want to switch to a dedicated struct for playout stats at some point. */
typedef struct ownermap {
	sig_atomic_t playouts;				/* Number of playouts */
	sig_atomic_t map[BOARD_MAX_COORDS][S_MAX];	/* Counts of final position colors for each coordinate. */

	move_stats_t avg_score;				/* Average score (white perspective) */
	move_stats_t score_sq_dev;			/* Average score squared deviations */
} ownermap_t;

void ownermap_init(ownermap_t *ownermap);
/* Display board and ownermap with score estimate from ownermap and playouts. */
void board_print_ownermap(board_t *b, FILE *f, ownermap_t *ownermap);
/* Fill ownermap at the end of playout */
void ownermap_fill(ownermap_t *ownermap, board_t *b, floating_t score);

/* Coord ownermap status: dame / black / white / unclear */
enum point_judgement ownermap_judge_point(ownermap_t *ownermap, coord_t c, floating_t thres);
/* Coord's owner if there is one with this threshold, otherwise S_NONE. */
enum stone ownermap_color(ownermap_t *ownermap, coord_t c, floating_t thres);
/* Coord's status from 1.0 (black) to 0.0 (white) */
float ownermap_estimate_point(ownermap_t *ownermap, coord_t c);

/* Find dead / unclear groups. */
void ownermap_dead_groups(board_t *b, ownermap_t *ownermap, mq_t *dead, mq_t *unclear);
/* Estimate status of stones on board based on ownermap stats. */
void ownermap_judge_groups(board_t *b, ownermap_t *ownermap, group_judgement_t *judge);
/* Add groups of given status to mq. */
void groups_of_status(board_t *b, group_judgement_t *judge, enum gj_state s, mq_t *mq);

/* Score estimate based on board ownermap. (positive: W wins) */
float ownermap_score_est(board_t *b, ownermap_t *ownermap);
/* Score estimate from color point of view (positive: color wins) */
float ownermap_score_est_color(board_t *b, ownermap_t *ownermap, enum stone color);
/* Score estimate as string */
char *ownermap_score_est_str(board_t *b, ownermap_t *ownermap);
/* Judge single coord for score estimation */
enum point_judgement ownermap_score_est_coord(board_t *b, ownermap_t *ownermap, coord_t c);

/* Playouts score estimate (average score) */
float playouts_score_est(ownermap_t *ownermap);
/* Playouts score estimate as string */
char *playouts_score_est_str(ownermap_t *ownermap);
/* Playouts score standard deviation */
float playouts_score_std_dev(ownermap_t *ownermap);

/* Raw count for each color. */
void ownermap_scores(board_t *b, ownermap_t *ownermap, int *scores);
int ownermap_dames(board_t *b, ownermap_t *ownermap);

/* Is board position final ? */
bool board_position_final(board_t *b, ownermap_t *ownermap, char **msg);
bool board_position_final_full(board_t *b, ownermap_t *ownermap,
			       mq_t *dead, mq_t *unclear, float score_est,
			       int *final_ownermap, int final_dames, float final_score, char **msg);

/* Don't allow passing earlier than that:
 * 19x19: 90    15x15: 56    13x13: 33    9x9: 13 */
#define board_earliest_pass(b)  (board_rsize2(b) / (7 - board_rsize(b) / 5))


#endif
