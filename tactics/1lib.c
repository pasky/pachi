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
	coord_t lib = group_lib(b, group, 0);

#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(group_libs(b, group) == 1);
	assert(board_at(b, lib) == S_NONE);
#endif
	if (immediate_liberty_count(b, lib) > 0 ||
	    group_stone_count(b, group, 2) > 1)
		return false;

	enum stone other_color = board_at(b, group);
	if (board_is_eyelike(b, lib, other_color))
		return false;
	
	foreach_neighbor(b, lib, {
		enum stone st = board_at(b, c);
		if (st == S_NONE || st == S_OFFBOARD)
			continue;

		group_t g = group_at(b, c);
		if (g == group)
			continue;

		if (st == other_color) {
			if (group_libs(b, g) == 1)  // capture more than one group
				return false;
		} else  // to_play
			if (group_libs(b, g) > 1)
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
	coord_t lib = group_lib(b, g, 0);

#ifdef EXTRA_CHECKS
	assert(is_player_color(to_play));
	assert(sane_group(b, g));
	assert(group_libs(b, g) == 1);
	assert(board_at(b, lib) == S_NONE);
#endif
	if (DEBUGL(6))  fprintf(stderr, "can capture group %s (%s)?\n", coord2sstr(g), coord2sstr(lib));
	
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, lib)
	    && !capturing_group_is_snapback(b, g))
		return true;

	return false;
}

static inline bool
can_play_on_lib(board_t *b, group_t g, enum stone to_play)
{
	coord_t lib = group_lib(b, g, 0);

#ifdef EXTRA_CHECKS
	assert(is_player_color(to_play));
	assert(sane_group(b, g));
	assert(board_at(b, lib) == S_NONE);
#endif
	if (DEBUGL(6))  fprintf(stderr, "can capture group %s (%s)?\n", coord2sstr(g), coord2sstr(lib));
	
	/* Does playing on the liberty usefully capture the group? */
	if (board_is_valid_play(b, to_play, lib)
	    && !is_bad_selfatari(b, to_play, lib))
		return true;

	return false;
}

/* Checks snapbacks.
 * We can't use b->clen, not maintained by board_quick_play(). */
bool
can_countercapture(board_t *b, group_t group, mq_t *q)
{
	enum stone color = board_at(b, group);
	enum stone other = stone_other(color);

#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(color));
#endif
	if (q) mq_init(q);
	int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (board_at(b, c) != other ||
			    group_libs(b, g) > 1 ||
			    !can_capture(b, g, color))
				continue;

			if (!q) return true;
			mq_add_nodup(q, group_lib(b, group_at(b, c), 0));
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
	enum stone color = board_at(b, group);
	enum stone other = stone_other(color);

#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(color));
#endif
	q->moves = 0;
	// Not checking b->clen, not maintained by board_quick_play()
	
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (likely(board_at(b, c) != other
				   || group_libs(b, g) > 1) ||
			    !can_capture(b, g, color))
				continue;

			mq_add_nodup(q, group_at(b, c));
		});
	} foreach_in_group_end;

	return (q->moves > 0);
}

/* Doesn't check snapbacks.
 * We can't use b->clen, not maintained by board_quick_play(). */
bool
can_countercapture_any(board_t *b, group_t group, mq_t *q)
{
	enum stone color = board_at(b, group);
	enum stone other = stone_other(color);

#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(color));
#endif
	int qmoves_prev = q ? q->moves : 0;

	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (board_at(b, c) != other ||
			    group_libs(b, g) > 1)
				continue;
			coord_t lib = group_lib(b, g, 0);
			if (!board_is_valid_play(b, color, lib))
				continue;

			if (!q) return true;
			mq_add_nodup(q, group_lib(b, group_at(b, c), 0));
		});
	} foreach_in_group_end;

	bool can = q ? q->moves > qmoves_prev : false;
	return can;
}


#ifdef NO_DOOMED_GROUPS
static bool
can_be_rescued(board_t *b, group_t group, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(color));
	assert(group_libs(b, group) == 1);
#endif
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
	enum stone color = board_at(b, group);
	coord_t lib = group_lib(b, group, 0);
	
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(to_play));
	assert(is_player_color(color));
	assert(group_libs(b, group) == 1);
	assert(board_at(b, lib) == S_NONE);
#endif

	if (DEBUGL(6))  fprintf(stderr, "group_atari_check group %s (%s)\n", coord2sstr(group), stone2str(color));

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
