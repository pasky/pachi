#ifndef PACHI_DISTRIBUTED_DISTRIBUTED_H
#define PACHI_DISTRIBUTED_DISTRIBUTED_H

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
#define max_parent_path(u, b) (((path_t)1) << (((u)->shared_levels - 1) * board_bits2(b)))


/* For debugging only */
struct hash_counts {
	long lookups;
	long collisions;
	long inserts;
	long occupied;
};

/* Find a hash table entry given its coord path from root.
 * Set found to false if the entry is empty.
 * Abort if the table gets too full (should never happen).
 * We use double hashing and coord_path = 0 for unused entries. */
#define find_hash(hash, table, hash_bits, path, found, counts)	\
	do { \
		if (DEBUG_MODE) counts.lookups++; \
		int mask = hash_mask(hash_bits); \
		int delta = (int)((path) >> (hash_bits)) | 1; \
		hash = ((int)(path) ^ delta ^ (delta >> (hash_bits))) & mask; \
		path_t cp = (table)[hash].coord_path; \
		found = (cp == path); \
		if (found | !cp) break; \
		int tries = 1 << ((hash_bits)-2); \
		do { \
			if (DEBUG_MODE) counts.collisions++; \
			hash = (hash + delta) & mask; \
			cp = (table)[hash].coord_path; \
			found = (cp == path); \
			if (found | !cp) break; \
		} while (--tries); \
		assert(tries); \
	} while (0)


/* Stats exchanged between master and slave. They are always
 * incremental values to be added to what was last sent. */
struct incr_stats {
	path_t coord_path;
	struct move_stats incr;
};

/* A slave machine updates at most 7 (19x19) or 9 (9x9) nodes for each
 * update of the root node. If we have at most 20 threads at 1500
 * games/s each, a slave machine can do at most 30K games/s. */

/* At 30K games/s a slave can output 270K nodes/s or 4.2 MB/s. The master
 * with a 100 MB/s network can thus support at most 24 slaves. */
#define DEFAULT_MAX_SLAVES 24

/* In a 30s move at 270K nodes/s a slave can send and receive at most
 * 8.1M nodes so at worst 23 bits are needed for the hash table in the
 * slave and for the per-slave hash table in the master. However the
 * same nodes are often sent so in practice 21 bits are sufficient.
 * Larger hash tables are not desirable because it would take too much
 * time to clear them at each move in the master. For the default
 * shared_levels=1, 18 bits are enough. */
#define DEFAULT_STATS_HBITS 18

/* If we select a cycle of at most 40ms, a slave machine can update at
 * most 10K different nodes per cycle. In practice the updates are
 * biased so we update much fewer nodes. As shorter cyle is preferable
 * because the stats are more fresh. The cycle time does not affect
 * the number of slaves and the hash table size. */
#define DEFAULT_SHARED_NODES 10240


/* Maximum game length. Power of 10 jut to ease debugging. */
#define DIST_GAMELEN 1000

#define force_reply(id)    ((id) + DIST_GAMELEN)
#define prevent_reply(id)  ((id) % DIST_GAMELEN)
#define move_number(id)    ((id) % DIST_GAMELEN)
#define reply_disabled(id) ((id) < DIST_GAMELEN)

char *path2sstr(path_t path, struct board *b);
struct engine *engine_distributed_init(char *arg, struct board *b);

#endif
