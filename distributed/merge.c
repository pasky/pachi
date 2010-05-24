/* The master keeps stats received from slaves in a queue of received
 * buffers that are merged together with the functions implemented
 * here. It also has one hash table per slave to maintain cumulative
 * stats that have not yet been sent to the slave machine. The queue
 * and the hash tables are cleared at each new move. */

#include <assert.h>
#include <stdio.h>
#include <limits.h>

#define DEBUG

#include "debug.h"
#include "timeinfo.h"
#include "distributed/distributed.h"
#include "distributed/merge.h"

/* We merge together debug stats for all hash tables. */
static struct hash_counts h_counts;

/* Display and reset hash statistics. For debugging only. */
void
merge_print_stats(int total_hnodes)
{
	if (DEBUGL(3)) {
		char buf[BSIZE];
		snprintf(buf, sizeof(buf),
			 "stats occupied %ld %.1f%% inserts %ld collisions %ld/%ld %.1f%%\n",
			 h_counts.occupied, h_counts.occupied * 100.0 / total_hnodes,
			 h_counts.inserts, h_counts.collisions, h_counts.lookups,
			 h_counts.collisions * 100.0 / (h_counts.lookups + 1));
		logline(NULL, "* ", buf);
	}
	if (DEBUG_MODE) h_counts.occupied = 0;
}

/* We maintain counts per bucket to avoid sorting large arrays.
 * All nodes with n updates since last send go to bucket n.
 * We have at most max_merged_nodes = (max_slaves-1) * shared_nodes
 * nodes to merge, 230K nodes for 24 slaves. If we put all nodes above
 * 1K updates in the top bucket, we get at most 230 nodes in this
 * bucket. So we can select exactly the best shared_nodes nodes if
 * shared_nodes >= 230. In practice there is overlap between
 * nodes sent by different slaves so shared_nodes can be lower. */
#define MAX_BUCKETS 1024

/* Update the hash table for the given increment stats,
 * and increment the bucket count. Return the hash index.
 * The slave lock is not held on either entry or exit of this function */
static inline int
stats_tally(struct incr_stats *s, struct slave_state *sstate, int *bucket_count)
{
	int h;
	bool found;
	struct incr_stats *stats_htable = sstate->stats_htable;
	find_hash(h, stats_htable, sstate->stats_hbits, s->coord_path, found, h_counts);
	if (found) {
		assert(stats_htable[h].incr.playouts > 0);
		stats_add_result(&stats_htable[h].incr, s->incr.value, s->incr.playouts);
	} else {
		stats_htable[h] = *s;
		if (DEBUG_MODE) h_counts.inserts++, h_counts.occupied++;
	}

	int incr = stats_htable[h].incr.playouts;
	if (incr >= MAX_BUCKETS) incr = MAX_BUCKETS - 1;
	bucket_count[incr]++;
	return h;
}

static struct incr_stats terminator = { .coord_path = INT64_MAX };

/* Initialize the next pointers (see merge_new_stats()).
 * Exclude invalid buffers and my own buffers by setting their next pointer
 * to a terminator value. Update min if there are too many nodes to merge,
 * so that merge time remains reasonable and the merge buffer doesn't overflow.
 * (We skip the oldest buffers if the slave thread is too much behind. It is
 * more important to get frequent incomplete updates than late complete updates.)
 * Return the total number of nodes to be merged.
 * The slave lock is not held on either entry or exit of this function. */
static int
filter_buffers(struct slave_state *sstate, struct incr_stats **next,
	       int *min, int max)
{
	int size = 0;
	int max_size = sstate->max_merged_nodes * sizeof(struct incr_stats);
 
	for (int q = max; q >= *min; q--) {
		if (!receive_queue[q] || receive_queue[q]->owner == sstate->thread_id) {
			next[q] = &terminator;
		} else if (size + receive_queue[q]->size > max_size) {
			*min = q + 1;
			assert(*min <= max);
			break;
		} else {
			next[q] = (struct incr_stats *)receive_queue[q]->buf;
			size += receive_queue[q]->size;
		}
	}
	return size / sizeof(struct incr_stats);
}

/* Return the minimum coord path of next[min..max].
 * This implementation is optimized for small values of max - min,
 * which is the case if slaves are not too much behind.
 * A heap (priority queue) could be used otherwise.
 * The returned value might be come from a buffer that has
 * been invalidated, the caller must check for this; in this
 * case the returned value is < the correct value. */
static inline path_t
min_coord(struct incr_stats **next, int min, int max)
{
	path_t min_c = next[min]->coord_path;
	for (int q = min + 1; q <= max; q++) {
		if (next[q]->coord_path < min_c)
			min_c = next[q]->coord_path;
	}
	return min_c;
}

/* Merge all valid incremental stats in receive_queue[min..max],
 * update the hash table, set the bucket counts, and save the
 * list of updated hash table entries. The input buffers and
 * the output buffer are all sorted by increasing coord path.
 * The input buffers end with a terminator value INT64_MAX.
 * Return the number of updated hash table entries. */

/* The slave lock is not held on either entry or exit of this function,
 * so receive_queue entries may be invalidated while we scan them.
 * The receive queue might grow while we scan it but we ignore
 * entries above max, they will be processed at the next call.
 * This function does not modify the receive queue. */
static int
merge_new_stats(struct slave_state *sstate, int min, int max,
		int *bucket_count, int *nodes_read, int last_queue_age)
{
	*nodes_read = 0;
	if (max < min) return 0;

	/* next[q] is the next value to be checked in receive_queue[q]->buf */
	struct incr_stats *next_[max - min + 1];
	struct incr_stats **next = next_ - min;
	*nodes_read = filter_buffers(sstate, next, &min, max);

	/* prev_min_c is only used for debugging. */
	path_t prev_min_c = 0;

	/* Do N-way merge, processing one coord path per iteration.
	 * If the minimum coord is INT64_MAX, either all buffers are
	 * invalidated, or at least one is valid and we are at the
	 * end of all valid buffers. In both cases we're done. */
	int merge_count = 0;
	path_t min_c;
	while ((min_c = min_coord(next, min, max)) != INT64_MAX) {

		struct incr_stats sum = { .coord_path = min_c,
					  .incr = { .playouts = 0, .value = 0.0 }};
		for (int q = min; q <= max; q++) {
			struct incr_stats s = *(next[q]);

			/* If s.coord_path != min_c, we must skip s.coord_path for now.
			 * If min_c is invalid, a future iteration will get a stable
			 * value since the call of min_coord(), so at some point we will
			 * get s.coord_path == min_c and we will not loop forever. */
			 if (s.coord_path != min_c) continue;

			/* We check the buffer validity after s.coord has been checked
			 * to avoid a race condition, and also to avoid multiple useless
			 * checks for the same coord_path. */
			if (unlikely(!receive_queue[q])) {
				next[q] = &terminator;
				continue;
			}

			/* Stop if we have a new move. If queue_age is incremented
			 * after this check, the merged output will be discarded. */
			if (unlikely(queue_age > last_queue_age)) return 0;

			/* s.coord_path is valid here, so min_c is valid too.
			 * (An invalid min_c would be < s.coord_path.) */
			assert(min_c > prev_min_c);

			assert(s.coord_path && s.incr.playouts);
			stats_add_result(&sum.incr, s.incr.value, s.incr.playouts);
			next[q]++;
		}
		/* All the buffers containing min_c may have been invalidated
		 * so sum may still be zero. But in this case the next[q] which
		 * contained min_c have been reset to &terminator so we will
		 * not loop forever. */
		if (!sum.incr.playouts) continue;

		assert(min_c > prev_min_c);
		if (DEBUG_MODE) prev_min_c = min_c;

		/* At this point sum contains only valid increments,
		 * so we can add it to the hash table. */
		assert(merge_count < sstate->max_merged_nodes);
		sstate->merged[merge_count++] = stats_tally(&sum, sstate, bucket_count);
	}
	return merge_count;
}

/* Save in buf the best increments from other slaves merged previously.
 * To avoid a costly scan of the entire hash table we only send nodes
 * that were previously sent recently by other slaves. It is possible
 * but very unlikely that the hash table contains some nodes with
 * higher number of playouts.
 * Return the number of nodes to be sent.
 * The slave lock is not held on either entry or exit of this function. */
static int
output_stats(struct incr_stats *buf, struct slave_state *sstate,
	     int *bucket_count, int merge_count)
{
	/* Find the minimum increment to send. The bucket with minimum
         * increment may be sent only partially. */
	int out_count = 0;
	int min_incr = MAX_BUCKETS;
	int shared_nodes = sstate->max_buf_size / sizeof(*buf);
	do {
		out_count += bucket_count[--min_incr];
	} while (min_incr > 1 && out_count < shared_nodes);

	/* Send all all increments > min_incr plus whatever we can at min_incr. */
	int min_count = bucket_count[min_incr] - (out_count - shared_nodes);
	out_count = 0;
	int *merged = sstate->merged;
	struct incr_stats *stats_htable = sstate->stats_htable;
	while (merge_count--) {
		int h = *merged++;
		int delta = stats_htable[h].incr.playouts - min_incr;
		if (delta < 0 || (delta == 0 && --min_count < 0)) continue;

		assert (out_count < shared_nodes);
		buf[out_count++] = stats_htable[h];

		/* Clear the hash table entry. (We could instead
		 * just clear the playouts but clearing the entry
		 * leads to fewer collisions later.) */
		stats_htable[h].coord_path = 0;
		if (DEBUG_MODE) h_counts.occupied--;
	} 
	/* The slave expects increments sorted by coord path
	 * but they are sorted already. */
	return out_count;
}

/* Get all incremental stats received from other slaves since the
 * last send. Store in buf the stats with largest playout increments.
 * Return the byte size of the resulting buffer. The caller must
 * check that the result is still valid.
 * The slave lock is held on both entry and exit of this function. */
static int
get_new_stats(struct incr_stats *buf, struct slave_state *sstate, int cmd_id)
{
	/* Process all valid buffers in receive_queue[min..max] */
	int min = sstate->last_processed + 1;
	int max = queue_length - 1;
	if (max < min && cmd_id == sstate->stats_id) return 0;

	sstate->last_processed = max;
	int last_queue_age = queue_age;

	/* It takes time to clear the hash table and merge the stats
	 * so do this unlocked. */
	protocol_unlock();

	double start = time_now();
	double clear_time = 0;

	/* Clear the hash table at a new move; the old paths in
	 * the hash table are now meaningless. */
	if (cmd_id != sstate->stats_id) {
		memset(sstate->stats_htable, 0, 
		       (1 << sstate->stats_hbits) * sizeof(sstate->stats_htable[0]));
		sstate->stats_id = cmd_id;
		clear_time = time_now() - start;
	}

	/* Set the bucket counts and update the hash table stats. */
	int bucket_count[MAX_BUCKETS];
	memset(bucket_count, 0, sizeof(bucket_count));
	int nodes_read;
	int merge_count = merge_new_stats(sstate, min, max, bucket_count,
					  &nodes_read, last_queue_age);

	int missed = 0;
	if (DEBUG_MODE)
		for (int q = min; q <= max; q++) missed += !receive_queue[q];

	/* Put the best increments in the output buffer. */
	int output_nodes = output_stats(buf, sstate, bucket_count, merge_count);

	if (DEBUGVV(2)) {
		char b[1024];
		snprintf(b, sizeof(b), "merged %d..%d missed %d %d/%d nodes,"
			 " output %d/%d nodes in %.3fms (clear %.3fms)\n",
			 min, max, missed, merge_count, nodes_read, output_nodes,
			 sstate->max_buf_size / (int)sizeof(*buf),
			 (time_now() - start)*1000, clear_time*1000);
		logline(&sstate->client, "= ", b);
	}

	protocol_lock();

	return output_nodes * sizeof(*buf);
}

/* Allocate the buffers in the merge specific part of the slave sate,
 * and reserve space for a terminator value (see merge_insert_hook). */
static void
merge_state_alloc(struct slave_state *sstate)
{
	sstate->stats_htable = calloc2(1 << sstate->stats_hbits, sizeof(struct incr_stats));
	sstate->merged = malloc2(sstate->max_merged_nodes * sizeof(int));
	sstate->max_buf_size -= sizeof(struct incr_stats);
}

/* Append a terminator value to make merge_new_stats() more
 * efficient. merge_state_alloc() has reserved enough space. */
static void
merge_insert_hook(struct incr_stats *buf, int size)
{
	int nodes = size / sizeof(*buf);
	buf[nodes].coord_path = INT64_MAX;
}

/* Initiliaze merge-related fields of the default slave state. */
void
merge_init(struct slave_state *sstate, int shared_nodes, int stats_hbits, int max_slaves)
{
	/* See merge_state_alloc() for shared_nodes + 1 */
	sstate->max_buf_size = (shared_nodes + 1) * sizeof(struct incr_stats);
	sstate->stats_hbits = stats_hbits;

	sstate->insert_hook = (buffer_hook)merge_insert_hook;
	sstate->alloc_hook = merge_state_alloc;
	sstate->args_hook = (getargs_hook)get_new_stats;

	/* At worst one late slave thread may have to merge up to
	 *   shared_nodes * BUFFERS_PER_SLAVE * (max_slaves - 1)
	 * nodes but on average it should not have to merge more than
	 *   dist->shared_nodes * (max_slaves - 1)
	 * Restricting the maximum number of merged nodes to the latter avoids
	 * spending excessive time on the merge. */
	sstate->max_merged_nodes = shared_nodes * (max_slaves - 1);
}
