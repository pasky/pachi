#ifndef PACHI_OWNERMAP_H
#define PACHI_OWNERMAP_H

/* Map of board intersection owners, and devices to derive group status
 * information from the map. */

#include <signal.h> // sig_atomic_t
struct move_queue;

/* How many games to consider at minimum before judging groups. */
#define GJ_MINGAMES	500

/* How big proportion of ownermap counts must be of one color to consider
 * the point sure. */
#define GJ_THRES	0.8

enum point_judgement {
	PJ_DAME = S_NONE,
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

struct group_judgement {
	floating_t thres;
	enum gj_state *gs; // [bsize2]
};

struct ownermap {
	/* Map of final owners of all intersections on the board. */
	/* This may be shared between multiple threads! */
	/* XXX: We assume sig_atomic_t is thread-atomic. This may not
	 * be true in pathological cases. */
	sig_atomic_t playouts;
	/* At the final board position, for each coordinate increase the
	 * counter of appropriate color. */
	sig_atomic_t map[BOARD_MAX_COORDS][S_MAX];
};

void ownermap_init(struct ownermap *ownermap);
void board_print_ownermap(struct board *b, FILE *f, struct ownermap *ownermap);
void ownermap_fill(struct ownermap *ownermap, struct board *b);
void ownermap_merge(int bsize2, struct ownermap *dst, struct ownermap *src);

/* Coord ownermap status: dame / black / white / unclear */
enum point_judgement ownermap_judge_point(struct ownermap *ownermap, coord_t c, floating_t thres);
/* Coord's owner if there is one with this threshold, otherwise S_NONE. */
enum stone ownermap_color(struct ownermap *ownermap, coord_t c, floating_t thres);
/* Coord's status from 1.0 (black) to 0.0 (white) */
float ownermap_estimate_point(struct ownermap *ownermap, coord_t c);

/* Find dead / unclear groups. */
void get_dead_groups(struct board *b, struct ownermap *ownermap, struct move_queue *dead, struct move_queue *unclear);
/* Estimate status of stones on board based on ownermap stats. */
void ownermap_judge_groups(struct board *b, struct ownermap *ownermap, struct group_judgement *judge);
/* Add groups of given status to mq. */
void groups_of_status(struct board *b, struct group_judgement *judge, enum gj_state s, struct move_queue *mq);

/* Score estimate based on board ownermap. (positive: W wins) */
float ownermap_score_est(struct board *b, struct ownermap *ownermap);
/* Score estimate from color point of view (positive: color wins) */
float ownermap_score_est_color(struct board *b, struct ownermap *ownermap, enum stone color);
char *ownermap_score_est_str(struct board *b, struct ownermap *ownermap);
enum point_judgement ownermap_score_est_coord(struct board *b, struct ownermap *ownermap, coord_t c);

/* Is board position final ? */
bool board_position_final(struct board *b, struct ownermap *ownermap, char **msg);
bool board_position_final_full(struct board *b, struct ownermap *ownermap,
			       struct move_queue *dead, struct move_queue *unclear, float score_est,
			       int *final_ownermap, int final_dames, float final_score,
			       char **msg, bool extra_checks);

/* Don't allow passing earlier than that:
 * 19x19: 120    15x15: 56    13x13: 33    9x9: 16 */
#define board_earliest_pass(b)  (real_board_size2(b) / (7 - 2 * real_board_size(b) / 9))


#endif
