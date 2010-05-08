#ifndef ZZGO_DISTRIBUTED_DISTRIBUTED_H
#define ZZGO_DISTRIBUTED_DISTRIBUTED_H

#include <limits.h>

#include "engine.h"
#include "stats.h"

/* A coord path encodes coordinates from root child to a given node:
 * A1->B2->C3 is encoded as coord(A1)<<18 + coord(B2)<<9 + coord(C3)
 * for 19x19. In this version the table is not a transposition table
 * so A1->B2->C3 and C3->B2->A1 are different.
 * The depth is limited to 7 for 19x19 (9 for 9x9) to fit in 64 bits.
 * path_t is signed to include pass and resign. */
typedef int64_t path_t;
#define PRIpath PRIx64
#define PATH_T_MAX INT64_MAX

#define hash_mask(bits) ((1<<(bits))-1)

/* parent_path() must never be used if path might be pass or resign. */
#define parent_path(path, board) ((path) >> board_bits2(board))
#define leaf_coord(path, board) ((path) & hash_mask(board_bits2(board)))
#define append_child(path, c, board) (((path) << board_bits2(board)) | (c))


/* Stats exchanged between master and slave. They are always
 * incremental values to be added to what was last sent. */
struct incr_stats {
	path_t coord_path;
	struct move_stats incr;
};

#define DIST_GAMELEN 1000

#define force_reply(id)    ((id) + DIST_GAMELEN)
#define prevent_reply(id)  ((id) % DIST_GAMELEN)
#define move_number(id)    ((id) % DIST_GAMELEN)
#define reply_disabled(id) ((id) < DIST_GAMELEN)

struct move_stats2 {
	struct move_stats u;
	struct move_stats amaf;
};

char *path2sstr(path_t path, struct board *b);
struct engine *engine_distributed_init(char *arg, struct board *b);

#endif
