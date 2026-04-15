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
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(group_libs(b, group) == 1);
#endif
	coord_t lib = group_lib(b, group, 0);

	if (immediate_liberty_count(b, lib) > 0 ||
	    !group_is_onestone(b, group))
		return false;

	enum stone other_color = board_at(b, group);  // group color
	if (board_is_eyelike(b, lib, other_color))
		return false;

	foreach_neighbor(b, lib, {
		group_t g = group_at(b, c);
		if (!g || g == group)  continue;

		if (board_at(b, c) == other_color) {
			if (group_libs(b, g) == 1)  // capture more than one group
				return false;
		} else                              // another group of our color
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
#ifdef EXTRA_CHECKS
	assert(is_player_color(to_play));
	assert(sane_group(b, g));
	assert(group_libs(b, g) == 1);
	assert(to_play == stone_other(board_at(b, g)));
#endif
	coord_t lib = group_lib(b, g, 0);

	if (DEBUGL(6))  fprintf(stderr, "can capture group %s (%s)?\n", coord2sstr(g), coord2sstr(lib));

	/* Does playing on the liberty usefully capture the group? */
	return (board_is_valid_play(b, to_play, lib) &&
		!capturing_group_is_snapback(b, g));
}

static inline bool
can_play_on_lib(board_t *b, group_t g, enum stone to_play)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(to_play));
	assert(sane_group(b, g));
	assert(board_at(b, g) == to_play);
#endif
	coord_t lib = group_lib(b, g, 0);

	if (DEBUGL(6))  fprintf(stderr, "can capture group %s (%s)?\n", coord2sstr(g), coord2sstr(lib));

	/* Can group escape by playing on liberty ? */
	return (board_is_valid_play_no_suicide(b, to_play, lib) &&
		!is_selfatari(b, to_play, lib));
}

/* Find group countercapture moves which are not snapbacks.
 * Note: We can't use b->clen, not maintained by board_quick_play(). */
bool
can_countercapture(board_t *b, group_t group, mq_t *q)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
#endif
	enum stone color = board_at(b, group);  // group color, to play
	enum stone other_color = stone_other(color);

	bool found = false;
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != other_color)  continue;

			group_t g = group_at(b, c);
			if (group_libs(b, g) == 1 && can_capture(b, g, color)) {
				if (!q) return true;
				mq_add_nodup(q, group_lib(b, g, 0));
				found = true;
			}
		});
	} foreach_in_group_end;

	return found;
}

/* Same as can_countercapture() but returns capturable groups instead of moves. */
bool
countercapturable_groups(board_t *b, group_t group, mq_t *q)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
#endif
	enum stone color = board_at(b, group);  // group color, to play
	enum stone other_color = stone_other(color);

	bool found = false;
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != other_color)  continue;

			group_t g = group_at(b, c);
			if (group_libs(b, g) == 1 && can_capture(b, g, color)) {
				mq_add_nodup(q, g);
				found = true;
			}
		});
	} foreach_in_group_end;

	return found;
}


#ifdef NO_DOOMED_GROUPS
static bool
can_be_rescued(board_t *b, group_t group, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(color));
	assert(group_libs(b, group) == 1);
	assert(color == board_at(b, group));
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
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group));
	assert(is_player_color(to_play));
	assert(group_libs(b, group) == 1);
#endif
	enum stone color = board_at(b, group);
	coord_t lib = group_lib(b, group, 0);

	if (DEBUGL(6))  fprintf(stderr, "group_atari_check group %s (%s)\n", coord2sstr(group), stone2str(color));

	if (to_play != color) {
		/* We are the attacker! In that case, do not try defending
		 * our group, since we can capture the culprit. */
#ifdef NO_DOOMED_GROUPS
		/* Do not remove group that cannot be saved by the opponent. */
		if (!can_be_rescued(b, group, color))
			return;
#endif
		if (can_capture(b, group, to_play))
			mq_add_nodup(q, lib);
		return;
	}

	/* Can we capture some neighbor? */
	bool ccap = can_countercapture(b, group, q);
	if (ccap && alwaysccaprate > fast_random(100))
		return;

	/* Otherwise, do not save kos.
	 * XXX Proper ko check should also check against multiple captures */
	if (group_is_onestone(b, group) && board_is_eyelike(b, lib, color)) {
		/* Except when the ko is for an eye! */
		bool eyeconnect = false;
		foreach_diag_neighbor(b, lib, {
			if (board_at(b, c) == S_NONE && board_is_eyelike(b, c, color)) {
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
