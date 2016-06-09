#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "tactics/1lib.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"


static inline bool
capturing_group_is_snapback(struct board *b, group_t group)
{	
	coord_t lib = board_group_info(b, group).lib[0];

	if (immediate_liberty_count(b, lib) > 0 ||
	    group_stone_count(b, group, 2) > 1)
		return false;
	
	enum stone to_play = stone_other(board_at(b, group));
	enum stone other = stone_other(to_play);	
	if (board_is_eyelike(b, lib, other))
		return false;
	
	foreach_neighbor(b, lib, {
			group_t g = group_at(b, c);
			if (board_at(b, c) == S_OFFBOARD || g == group)
				continue;
			
			if (board_at(b, c) == other &&
			    board_group_info(b, g).libs == 1)  // capture more than one group
				return false;
			if (board_at(b, c) == to_play &&
			    board_group_info(b, g).libs > 1)  
				return false;
		});
	return true;
}

/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


static inline bool
can_capture(struct board *b, group_t g, enum stone to_play)
{
	coord_t capture = board_group_info(b, g).lib[0];
	if (DEBUGL(6))
		fprintf(stderr, "can capture group %d (%s)?\n",
			g, coord2sstr(capture, b));
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, capture)
	    && !capturing_group_is_snapback(b, g))
		return true;

	return false;
}

static inline bool
can_play_on_lib(struct board *b, group_t g, enum stone to_play)
{
	coord_t capture = board_group_info(b, g).lib[0];
	if (DEBUGL(6))
		fprintf(stderr, "can capture group %d (%s)?\n",
			g, coord2sstr(capture, b));
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, capture)
	    && !is_bad_selfatari(b, to_play, capture))
		return true;

	return false;
}

/* Check snapbacks */
static inline __attribute__((always_inline)) bool
capturable_group(struct board *b, enum stone capturer, coord_t c,
		 enum stone to_play)
{
	group_t g = group_at(b, c);
	if (likely(board_at(b, c) != stone_other(capturer)
	           || board_group_info(b, g).libs > 1))
		return false;

	return can_capture(b, g, to_play);
}

bool
can_countercapture(struct board *b, enum stone owner, group_t g,
		   enum stone to_play, struct move_queue *q, int tag)
{
	//if (!b->clen)
	//	return false;

	unsigned int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, g) {
		foreach_neighbor(b, c, {
			if (!capturable_group(b, owner, c, to_play))
				continue;

			if (!q) {
				return true;
			}
			mq_add(q, board_group_info(b, group_at(b, c)).lib[0], tag);
			mq_nodup(q);
		});
	} foreach_in_group_end;

	bool can = q ? q->moves > qmoves_prev : false;
	return can;
}

static inline __attribute__((always_inline)) bool
capturable_group_fast(struct board *b, enum stone capturer, coord_t c,
		      enum stone to_play)
{
	group_t g = group_at(b, c);
	if (likely(board_at(b, c) != stone_other(capturer)
	           || board_group_info(b, g).libs > 1))
		return false;

	coord_t lib = board_group_info(b, g).lib[0];
	return board_is_valid_play(b, to_play, lib);
}

bool
can_countercapture_any(struct board *b, enum stone owner, group_t g,
		       enum stone to_play, struct move_queue *q, int tag)
{
	//if (b->clen < 2)
	//	return false;

	unsigned int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, g) {
		foreach_neighbor(b, c, {
			if (!capturable_group_fast(b, owner, c, to_play))
				continue;

			if (!q) {
				return true;
			}
			mq_add(q, board_group_info(b, group_at(b, c)).lib[0], tag);
			mq_nodup(q);
		});
	} foreach_in_group_end;

	bool can = q ? q->moves > qmoves_prev : false;
	return can;
}

#if 0
bool
can_countercapture_(struct board *b, enum stone owner, group_t g,
		   enum stone to_play, struct move_queue *q, int tag)
{
	if (!g || 
	    board_at(b, g) != owner || 
	    owner != to_play) {
		board_print(b, stderr);
		fprintf(stderr, "can_countercap(%s %s %s):  \n",
			stone2str(owner), coord2sstr(g, b), stone2str(to_play));
	}
	/* Sanity checks */
	assert(g);
	assert(board_at(b, g) == owner);
	assert(owner == to_play);

#if 0
	bool r1 = my_can_countercapture(b, owner, g, to_play, NULL, 0);
	bool r2 = orig_can_countercapture(b, owner, g, to_play, NULL, 0);
	if (r1 != r2) {
		fprintf(stderr, "---------------------------------------------------------------\n");
		board_print(b, stderr);
		fprintf(stderr, "can_countercap(%s %s %s) diff !   my:%i   org:%i\n",
			stone2str(owner), coord2sstr(g, b), stone2str(to_play), r1, r2);
	}
#endif
	return orig_can_countercapture(b, owner, g, to_play, q, tag);
}
#endif

#ifdef NO_DOOMED_GROUPS
static bool
can_be_rescued(struct board *b, group_t group, enum stone color, int tag)
{
	/* Does playing on the liberty rescue the group? */
	if (can_play_on_lib(b, group, color))
		return true;

	/* Then, maybe we can capture one of our neighbors? */
	return can_countercapture(b, color, group, color, NULL, tag);
}
#endif

void
group_atari_check(unsigned int alwaysccaprate, struct board *b, group_t group, enum stone to_play,
                  struct move_queue *q, coord_t *ladder, bool middle_ladder, int tag)
{
	enum stone color = board_at(b, group_base(group));
	coord_t lib = board_group_info(b, group).lib[0];

	assert(color != S_OFFBOARD && color != S_NONE);
	if (DEBUGL(5))
		fprintf(stderr, "[%s] atariiiiiiiii %s of color %d\n",
		        coord2sstr(group, b), coord2sstr(lib, b), color);
	assert(board_at(b, lib) == S_NONE);

	if (to_play != color) {
		/* We are the attacker! In that case, do not try defending
		 * our group, since we can capture the culprit. */
#ifdef NO_DOOMED_GROUPS
		/* Do not remove group that cannot be saved by the opponent. */
		if (!can_be_rescued(b, group, color, tag))
			return;
#endif
		if (can_play_on_lib(b, group, to_play)) {
			mq_add(q, lib, tag);
			mq_nodup(q);
		}
		return;
	}

	/* Can we capture some neighbor? */
	/* XXX Attempts at using new can_countercapture() here failed so far.
	 *     Could be because of a bug / under the stones situations
	 *     (maybe not so uncommon in moggy ?) / it upsets moggy's balance somehow
	 *     (there's always a chance opponent doesn't capture after taking snapback) */
	bool ccap = can_countercapture_any(b, color, group, to_play, q, tag);
	if (ccap && !ladder && alwaysccaprate > fast_random(100))
		return;

	/* Otherwise, do not save kos. */
	if (group_is_onestone(b, group)
	    && neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) == 4) {
		/* Except when the ko is for an eye! */
		bool eyeconnect = false;
		foreach_diag_neighbor(b, lib) {
			if (board_at(b, c) == S_NONE && neighbor_count_at(b, c, color) + neighbor_count_at(b, c, S_OFFBOARD) == 4) {
				eyeconnect = true;
				break;
			}
		} foreach_diag_neighbor_end;
		if (!eyeconnect)
			return;
	}

	/* Do not suicide... */
	if (!can_play_on_lib(b, group, to_play))
		return;
	if (DEBUGL(6))
		fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders (unless we can counter-capture anytime). */
	if (!ccap) {
		if (is_ladder(b, lib, group, middle_ladder)) {
			/* Sometimes we want to keep the ladder move in the
			 * queue in order to discourage it. */
			if (!ladder)
				return;
			else
				*ladder = lib;
		} else if (DEBUGL(6))
			fprintf(stderr, "...no ladder\n");
	}

	mq_add(q, lib, tag);
	mq_nodup(q);
}
