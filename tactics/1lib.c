#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

//#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "tactics/1lib.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"


bool
capturing_group_is_snapback(board_t *b, group_t group)
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
			
			if (board_at(b, c) == other && board_group_info(b, g).libs == 1)  // capture more than one group
				return false;
			if (board_at(b, c) == to_play && board_group_info(b, g).libs > 1)
				return false;
		});
	return true;
}

/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


bool
can_capture(board_t *b, group_t g, enum stone to_play)
{
	//assert(g && board_group_info(b, g).libs == 1);
	coord_t capture = board_group_info(b, g).lib[0];
	if (DEBUGL(6))  fprintf(stderr, "can capture group %d (%s)?\n", g, coord2sstr(capture));
	
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, capture)
	    && !capturing_group_is_snapback(b, g))
		return true;

	return false;
}

static inline bool
can_play_on_lib(board_t *b, group_t g, enum stone to_play)
{
	coord_t capture = board_group_info(b, g).lib[0];
	if (DEBUGL(6))  fprintf(stderr, "can capture group %d (%s)?\n", g, coord2sstr(capture));
	
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, capture)
	    && !is_bad_selfatari(b, to_play, capture))
		return true;

	return false;
}

/* Checks snapbacks */
bool
can_countercapture(board_t *b, group_t group, mq_t *q)
{
	if (q) mq_init(q);
	enum stone color = board_at(b, group);
	enum stone other = stone_other(color);
	assert(color == S_BLACK || color == S_WHITE);	
	// Not checking b->clen, not maintained by board_quick_play()
	
	int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (board_at(b, c) != other ||
			    board_group_info(b, g).libs > 1 ||
			    !can_capture(b, g, color))
				continue;

			if (!q) return true;
			mq_add_nodup(q, board_group_info(b, group_at(b, c)).lib[0]);
		});
	} foreach_in_group_end;

	bool can = q ? q->moves > qmoves_prev : false;
	return can;
}

/* Same as can_countercapture() but returns capturable groups instead of moves,
 * queue may not be NULL, and is always cleared. */
bool
countercapturable_groups(board_t *b, group_t group, mq_t *q)
{
	q->moves = 0;
	enum stone color = board_at(b, group);
	enum stone other = stone_other(color);
	assert(color == S_BLACK || color == S_WHITE);	
	// Not checking b->clen, not maintained by board_quick_play()
	
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (likely(board_at(b, c) != other
				   || board_group_info(b, g).libs > 1) ||
			    !can_capture(b, g, color))
				continue;

			mq_add_nodup(q, group_at(b, c));
		});
	} foreach_in_group_end;

	return (q->moves > 0);
}

bool
can_countercapture_any(board_t *b, group_t group, mq_t *q)
{
	enum stone color = board_at(b, group);
	enum stone other = stone_other(color);
	assert(color == S_BLACK || color == S_WHITE);
	// Not checking b->clen, not maintained by board_quick_play()
	
	int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (board_at(b, c) != other ||
			    board_group_info(b, g).libs > 1)
				continue;
			coord_t lib = board_group_info(b, g).lib[0];
			if (!board_is_valid_play(b, color, lib))
				continue;

			if (!q) return true;
			mq_add_nodup(q, board_group_info(b, group_at(b, c)).lib[0]);
		});
	} foreach_in_group_end;

	bool can = q ? q->moves > qmoves_prev : false;
	return can;
}


#ifdef NO_DOOMED_GROUPS
static bool
can_be_rescued(board_t *b, group_t group, enum stone color)
{
	/* Does playing on the liberty rescue the group? */
	if (can_play_on_lib(b, group, color))
		return true;

	/* Then, maybe we can capture one of our neighbors? */
	return can_countercapture(b, group, NULL);
}
#endif

void
group_atari_check(unsigned int alwaysccaprate, board_t *b, group_t group, enum stone to_play,
                  mq_t *q, bool middle_ladder)
{
	enum stone color = board_at(b, group_base(group));
	coord_t lib = board_group_info(b, group).lib[0];

	assert(color != S_OFFBOARD && color != S_NONE);
	if (DEBUGL(6))  fprintf(stderr, "group_atari_check group %s (%s)\n", coord2sstr(group), stone2str(color));
	assert(board_at(b, lib) == S_NONE);

	if (to_play != color) {
		/* We are the attacker! In that case, do not try defending
		 * our group, since we can capture the culprit. */
#ifdef NO_DOOMED_GROUPS
		/* Do not remove group that cannot be saved by the opponent. */
		if (!can_be_rescued(b, group, color))
			return;
#endif
		if (can_play_on_lib(b, group, to_play))
			mq_add_nodup(q, lib);
		return;
	}

	/* Can we capture some neighbor? */
	/* XXX Attempts at using new can_countercapture() here failed so far.
	 *     Could be because of a bug / under the stones situations
	 *     (maybe not so uncommon in moggy ?) / it upsets moggy's balance somehow
	 *     (there's always a chance opponent doesn't capture after taking snapback) */
	bool ccap = can_countercapture_any(b, group, q);
	if (ccap && alwaysccaprate > fast_random(100))
		return;

	/* Otherwise, do not save kos. */
	if (group_is_onestone(b, group)
	    && neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) == 4) {
		/* Except when the ko is for an eye! */
		bool eyeconnect = false;
		foreach_diag_neighbor(b, lib, {
			if (board_at(b, c) == S_NONE && neighbor_count_at(b, c, color) + neighbor_count_at(b, c, S_OFFBOARD) == 4) {
				eyeconnect = true;
				break;
			}
		});
		if (!eyeconnect)  return;
	}

	/* Do not suicide... */
	if (!can_play_on_lib(b, group, to_play))
		return;
	if (DEBUGL(6))  fprintf(stderr, "...escape route valid\n");
	
	/* ...or play out ladders (unless we can counter-capture anytime). */
	if (!ccap) {
		if (is_ladder(b, group, middle_ladder))
			return;
		else if (DEBUGL(6))  fprintf(stderr, "...no ladder\n");
	}

	mq_add_nodup(q, lib);
}
