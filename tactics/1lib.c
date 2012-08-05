#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "tactics/1lib.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"


/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


static bool
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

/* For given position @c, decide if this is a group that is in danger from
 * @capturer and @to_play can do anything about it (play at the last
 * liberty to either capture or escape). */
/* Note that @to_play is important; e.g. consider snapback, it's good
 * to play at the last liberty by attacker, but not defender. */
static inline __attribute__((always_inline)) bool
capturable_group(struct board *b, enum stone capturer, coord_t c,
		 enum stone to_play)
{
	group_t g = group_at(b, c);
	if (likely(board_at(b, c) != stone_other(capturer)
	           || board_group_info(b, g).libs > 1))
		return false;

	return can_play_on_lib(b, g, to_play);
}

bool
can_countercapture(struct board *b, enum stone owner, group_t g,
		   enum stone to_play, struct move_queue *q, int tag)
{
	if (b->clen < 2)
		return false;

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
	bool ccap = can_countercapture(b, color, group, to_play, q, tag);
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
