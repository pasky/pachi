#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

//#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "random.h"
#include "tactics/1lib.h"
#include "tactics/selfatari.h"
#include "tactics/dragon.h"
#include "tactics/seki.h"
#include "nakade.h"


/**********************************************************************************/
/* Selfatari state */

static inline bool
group_is_selfatari_neighbor(selfatari_state_t *s, board_t *b, group_t g, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_group(b, g));
#endif
	for (int i = 0; i < s->groupcts[color]; i++)
		if (g == s->groupids[color][i])
			return true;
	return false;
}

static void
init_selfatari_state(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
#endif
	// memset() slower here ...
	s->groupcts[S_BLACK] = s->groupcts[S_WHITE] = 0;
	s->lib = pass;
	s->extra_lib = pass;
	s->captures = 0;

	foreach_neighbor(b, to, {
		enum stone col = board_at(b, c);
		if (col == S_NONE) {  s->lib = c; continue;  }

		group_t g = group_at(b, c);
		if (!g)  continue;

		s->captures += (col != color && group_libs(b, g) == 1);

		if (!group_is_selfatari_neighbor(s, b, g, col))
			s->groupids[col][s->groupcts[col]++] = g;
	});
}


/**********************************************************************************/
/* Friendly groups */

static bool
three_liberty_suicide(board_t *b, group_t g, enum stone color, coord_t to, selfatari_state_t *s)
{
	/* If a group has three liberties, by playing on one of
	 * them it is possible to kill the group clumsily. Check
	 * against that condition: "After our move, the opponent
	 * can unconditionally capture the group."
	 *
	 * Examples:
	 *
	 * O O O O O O O   X X O O O O O O     v-v- ladder
	 * O X X X X X O   . O X X X X X O   . . . O O
	 * O X ! . ! X O   . O X ! . ! O .   O X X . O
	 * O X X X X X O   # # # # # # # #   O O O O O */
#ifdef EXTRA_CHECKS
	assert(sane_group(b, g));
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == S_NONE);
#endif
	/* Extract the other two liberties. */
	coord_t other_libs[2];
	bool other_libs_adj[2];
	for (int i = 0, j = 0; i < 3; i++) {
		coord_t lib = group_lib(b, g, i);
		if (lib != to) {
			other_libs_adj[j] = coord_is_adjecent(lib, to);
			other_libs[j++] = lib;
		}
	}

	/* Make sure this move is not useful by gaining liberties,
	 * splitting the other two liberties (quite possibly splitting
	 * 3-eyespace!) or connecting to a different group. */
	if (immediate_liberty_count(b, to) - (other_libs_adj[0] || other_libs_adj[1]) > 0)
		return false;
	assert(!(other_libs_adj[0] && other_libs_adj[1]));
	if (s->groupcts[color] > 1)
		return false;

	/* Playing on the third liberty might be useful if it enables
	 * capturing some group (are we doing nakade or semeai?). */
	enum stone other_color = stone_other(color);
	for (int i = 0; i < s->groupcts[other_color]; i++)
		if (group_libs(b, s->groupids[other_color][i]) <= 3)
			return false;


	/* Okay. This looks like a pretty dangerous situation. The
	 * move looks useless, it definitely converts us to a 2-lib
	 * group. But we still want to play it e.g. if it takes off
	 * liberties of some unconspicous enemy group, and of course
	 * also at the game end to leave just single-point eyes. */

	if (DEBUGL(6))
		fprintf(stderr, "3-lib danger\n");

	/* Therefore, the final suicidal test is: (After filling this
	 * liberty,) when opponent fills liberty [0], playing liberty
	 * [1] will not help the group, or vice versa. */
	bool other_libs_neighbors = coord_is_adjecent(other_libs[0], other_libs[1]);
	for (int i = 0; i < 2; i++) {
		int null_libs = other_libs_neighbors + other_libs_adj[i];
		if (board_is_one_point_eye(b, other_libs[1 - i], color)) {
			/* The other liberty is an eye, happily go ahead.
			 * There are of course situations where this will
			 * take off semeai liberties, but without this check,
			 * many terminal endgame plays will be messed up. */
			return false;
		}
		if (immediate_liberty_count(b, other_libs[i]) - null_libs > 1) {
			/* Gains liberties. */
			/* TODO: Check for ladder! */
next_lib:
			continue;
		}
		foreach_neighbor(b, other_libs[i], {
			if (board_at(b, c) == color
			    && group_at(b, c) != g
			    && group_libs(b, group_at(b, c)) > 1) {
				/* Can connect to a friend. */
				/* TODO: > 2? But maybe the group can capture
				 * a neighbor! But then better let it do that
				 * first? */
				goto next_lib;
			}
		});
		/* If we can capture a neighbor, better do it now
		 * before wasting a liberty. So no need to check. */
		/* Ok, the last liberty has no way to get out. */
		if (DEBUGL(6))
			fprintf(stderr, "3-lib dangerous: %s\n", coord2sstr(other_libs[i]));
		return true;
	}

	return false;
}

static int
examine_friendly_groups(board_t *b, enum stone color, coord_t to, selfatari_state_t *s, int flags)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));	
	assert(is_player_color(color));
	assert(board_at(b, to) == S_NONE);
#endif	
	for (int i = 0; i < s->groupcts[color]; i++) {
		/* We can escape by connecting to this group if it's not in atari. */
		group_t g = s->groupids[color][i];

		if (group_libs(b, g) > 2) {
			/* Could we self-atari the group here? */
			if (flags & SELFATARI_3LIB_SUICIDE &&
			    group_libs(b, g) == 3
			    && three_liberty_suicide(b, g, color, to, s))
				return true;
			return false;
		}

		if (group_libs(b, g) != 2)
			continue;

		/* Group has 2 liberties: It provides an extra liberty.
		 * We need another liberty besides this one. */
		int lib2 = group_other_lib(b, g, to);

		/* Do we have the liberty locally ? */
		if (!is_pass(s->lib) && s->lib != lib2)
			return false;

		/* Or another extra liberty ? */
		if (!is_pass(s->extra_lib) && s->extra_lib != lib2)
			return false;

		/* Save extra liberty. */
		s->extra_lib = lib2;
	}

	return -1;
}


/**********************************************************************************/
/* Enemy groups */

static int
examine_enemy_groups(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));	
	assert(is_player_color(color));
	assert(board_at(b, to) == S_NONE);
#endif
	if (s->captures) {
		/* Capturing gives one liberty, we need another one.
		 * Do we already have an extra liberty ? */
		if (!is_pass(s->lib) || !is_pass(s->extra_lib))
			return false;

		/* Ko captures and multiple captures are fine. */
		int friendly_groups = s->groupcts[color];
		if (!friendly_groups || s->captures > 1)
			return false;

		/* Group capture ? */
		enum stone other_color = stone_other(color);
		for (int i = 0; i < s->groupcts[other_color]; i++) {
			group_t g = s->groupids[other_color][i];
			if (group_libs(b, g) != 1) continue;

			/* Capturing more than one stone is always nice ! */
			if (!group_is_onestone(b, g))
				return false;
		}
	} else if (is_pass(s->extra_lib) && is_pass(s->lib)) {
		/* This move is a suicide, not even a self-atari. */
		if (DEBUGL(6))  fprintf(stderr, "suicide\n");
		return true;
	}

	/* Don't catch snapback captures here, some of them are good selfataris
	 * like throw-in to falsify eye. Let them through, if they don't get
	 * matched later on they will get rejected anyway. */

	return -1;
}


/**********************************************************************************/
/* Throw-in */

/* We can be throwing-in to false eye:
 *   . . O O O O O O O O O . .
 *   X X X O X X X O X X X X X
 *   X . * X * O . X * O O . X
 *   -------------------------  */
static int
check_throwin(board_t *b, enum stone color, coord_t to, group_t own_group)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
	if (own_group) {
		assert(sane_group(b, own_group));
		assert(board_at(b, own_group) == color);
	}
#endif
	enum stone other_color = stone_other(color);

	/* We cannot sensibly throw-in into a corner. */
	if (neighbor_count_at(b, to, S_OFFBOARD) == 2)
		return -1;

	/* XXX Should also check crosscut pattern, lets weird stuff through without.
	 *     Adding it hurts playouts balance quite a bit though, investigate ... */

	/* Throw-in situation ? */
	if (!(neighbor_count_at(b, to, other_color) + neighbor_count_at(b, to, S_OFFBOARD) == 3 &&
	      board_is_false_eyelike(b, to, other_color)))
		return -1;

	/* Single-stone throw-in may be ok... */
	if (!own_group) {
		/*  O X .    . X .   There is one problem:
		 *  . * X    . * X   When it's actually not a throw-in !
		 *  -----    -----                                      */
		foreach_neighbor(b, to, {
			if (board_at(b, c) != S_NONE) continue;
			/* Is the empty neighbor an escape path?
			 * (Note that one S_NONE neighbor is already @to.)
			 * We need at least 2 opponent stones nearby (1 at the edge). */
			int other_stones = neighbor_count_at(b, c, other_color);
			if (!other_stones ||
			    other_stones + neighbor_count_at(b, c, S_OFFBOARD) < 2)
				return true;
			/* Shape must be right also: Stones above/below empty spot
			 * in example must be either black or offboard. */
			int offset = other_offset(c - to);
			enum stone e1 = board_at(b, c + offset);
			enum stone e2 = board_at(b, c - offset);
			if ((e1 != other_color && e1 != S_OFFBOARD) ||
			    (e2 != other_color && e2 != S_OFFBOARD))
				return true;
		});
		return false;
	}

	/* Multi-stone throwin:
	 * In that case, we must be connected to at most one stone,
	 * or throwin will not destroy any eyes. */
	group_t g = own_group;
	if (!group_is_onestone(b, g))
		return -1;

	/* We are in one of these situations:
	 * 1) O O O X .  2) . . O O X .  3) . X O O X .  4) O O O X .
	 *    . X . O .     . . X . O .     . . X . O .     O X . O X
	 *    ---------     -----------     -----------     ---------
	 *
	 * good: 1), 4)    bad: 2), 3)                               */
	if (group_libs(b, g) == 2) {  // 1), 2) or 3)
		/* Must be somewhat enclosed */
		coord_t other_lib = group_other_lib(b, g, to);
		int other_own = neighbor_count_at(b, other_lib, color) - 1;
		if (immediate_liberty_count(b, other_lib) + other_own < 2)
			return false;  // 1)
		return true;           // 2), 3)
	}

	/* 4): Handled later in setup_nakade().
	 * Just do some sanity checks. */
#ifdef EXTRA_CHECKS
	assert(group_libs(b, g) == 1);
	assert(board_get_atari_neighbor(b, to, other_color));
#endif
	return -1;
}


/**********************************************************************************/
/* Snapback */

/*   . X O .	Check if playing at @to snapbacks 2lib group @group.
 *   X . * O	@lib is throw-in stone's liberty.
 *   X O O X	Check that capture captures only our stone, and that lib's neighbors
 *   . X X X    are sane (no extra lib, no opponent group with more than 2 libs) */
static bool
capturing_would_be_snapback_for(board_t *b, enum stone color, group_t group, coord_t to, coord_t lib)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_group(b, group));
	assert(sane_coord(to));
	assert(sane_coord(lib));
	assert(board_at(b, to) == S_NONE);
	assert(board_at(b, lib) == S_NONE);
#endif
	foreach_neighbor(b, lib, {
		enum stone col = board_at(b, c);
		if (col == S_NONE || col == S_OFFBOARD)
			continue;

		group_t g = group_at(b, c);
		int libs = group_libs(b, g);
		if (col == color) {
			if (libs == 1)  // capture more than one group
				return false;
		} else { // other_color
			if (libs > 2)   // will gain extra libs
				return false;
			if (libs == 2 && group_other_lib(b, g, lib) != to)
				return false;
		}
	});
	return true;
}

/* Check if selfatari stone sets up a snapback. */
static int
check_snapback(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
	assert(board_at(b, to) == S_NONE);
	/* Single-stone throw-in should have one liberty, captures have been checked. */
	if (!s->groupcts[color])
		assert(sane_coord(s->lib));
#endif
	enum stone other_color = stone_other(color);

	/* Must be single-stone throw-in. */
	if (s->groupcts[color])
		return -1;

	/* Cramped space */
	if (immediate_liberty_count(b, s->lib) != 1)
		return -1;

	/* Not ko-like situation */
	if (!neighbor_count_at(b, s->lib, other_color))
		return -1;

	/* Examine enemy groups */
	for (int i = 0; i < s->groupcts[other_color]; i++) {
		group_t g = s->groupids[other_color][i];
		if (group_libs(b, g) == 2 &&
		    capturing_would_be_snapback_for(b, color, g, to, s->lib)) {
			s->snapback_group = g;
			return false;
		}
	}
	return -1;
}


/**********************************************************************************/
/* Nakade */

static bool
unreachable_lib_from_neighbors(board_t *b, enum stone color, coord_t to, selfatari_state_t *s,
			       coord_t lib)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(sane_coord(lib));
#endif	
	for (int i = 0; i < s->groupcts[color]; i++) {
		group_t g = s->groupids[color][i];
		for (int j = 0; j < group_libs(b, g); j++)
			if (group_lib(b, g, j) == lib)
				return false;
	}
	return true;
}

static bool
unreachable_lib(board_t *b, enum stone color, coord_t to, selfatari_state_t *s,
		coord_t lib)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(sane_coord(lib));
	assert(board_at(b, lib) == S_NONE);
#endif
	if (coord_is_adjecent(to, lib))
		return false;
	return unreachable_lib_from_neighbors(b, color, to, s, lib);
}

/* Instead of playing this self-atari, could we have connected/escaped by 
 * playing on the other liberty of a neighboring group ? */
static inline bool
is_bad_nakade(board_t *b, enum stone color, coord_t to, coord_t lib2, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(sane_coord(lib2));
	assert(board_at(b, to) == S_NONE);
	assert(board_at(b, lib2) == S_NONE);
#endif
	/* If the other liberty has empty neighbor, it must be the original liberty;
         * otherwise, since the whole group has only 2 liberties, the other liberty
         * may not be internal and we are nakade'ing eyeless group from outside,
         * which is stupid. */
	foreach_neighbor(b, lib2, {
		if (c != to && board_at(b, c) == S_NONE)
			return true;
	});

	/* Let's look at neighbors of the other liberty:
	 * If there's a group we don't know about it's a bad nakade
	 * (should connect these groups / play outside liberty instead) */
	foreach_neighbor(b, lib2, {
		if (board_at(b, c) != color)  continue;
		group_t g2 = group_at(b, c);
		if (!group_is_selfatari_neighbor(s, b, g2, color))
			return true;
	});

	/* Check if the other liberty is internal:
	 * If we can't reach it from our groups for sure it's not internal. */
	if (unreachable_lib(b, color, to, s, lib2))
		return true;

	return false;
}


/* Instead of playing this self-atari, could we have connected/escaped by 
 * playing on the other liberty of a neighboring group ? */
static inline bool
can_escape_instead(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
#endif
	for (int i = 0; i < s->groupcts[color]; i++) {
		group_t g = s->groupids[color][i];
		if (group_libs(b, g) != 2)
			continue;
		coord_t other = group_other_lib(b, g, to);
		
		/* Let's look at the other liberty of that group. */
		if (immediate_liberty_count(b, other) >= 2 ||      /* Can escape ! */
		    is_bad_nakade(b, color, to, other, s))  /* Should connect instead */
			return true;
	}
	return false;
}

/* Nakade area after playing selfatari + capture */
static void
selfatari_nakade_area(board_t *b, selfatari_state_t *s, enum stone color, coord_t to, mq_t *area)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
#endif
	mq_init(area);
	for (int i = 0; i < s->groupcts[color]; i++) {
		group_t g = s->groupids[color][i];
		foreach_in_group(b, g) {
			mq_add(area, c);
		} foreach_in_group_end;
	}

	mq_add(area, to);  /* Selfatari move */
}

/* Only cares about dead shape (and not screwing up sekis) */
static bool
nakade_making_dead_shape(board_t *b, enum stone color, coord_t to, selfatari_state_t *s, int stones)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(stones > 2);
	assert(stones <= 6);
#endif
	if (stones >= 4) {
		mq_t area;  selfatari_nakade_area(b, s, color, to, &area);
		if (!nakade_area_dead_shape(b, &area))
			return false;
	}

	/* If breaking seki check we are really atariing *everything* around us from the inside */
	if (breaking_local_seki(b, s, to))
		with_move_strict(b, to, color, {  /* Play self-atari */
			enum stone other_color  = stone_other(color);
			group_t g = group_at(b, to);

			foreach_in_group(b, g) {
				foreach_neighbor(b, c, {
					if (board_at(b, c) != other_color) continue;
					group_t g2 = group_at(b, c);
					if (group_libs(b, g2) > 1)
						with_move_return(false);  /* Surrounding group not in atari */
					});
			} foreach_in_group_end;
		});
	return true;
}

#if 0
/* Fast but there are issues with this (triangle six is not dead !)
 * We also need to know status if opponent plays first */
static inline int
nakade_making_dead_shape_hack(board_t *b, enum stone color, coord_t to, int lib2,
			      selfatari_state_t *s, int pre_selfatari_stones)
{
	/* It also remains to be seen whether it is nakade
	 * and not seki destruction. To do this properly, we
	 * would have to look at the group shape. But we can
	 * cheat too! Brett Combs helps to introduce a static
	 * rule that should in fact cover *all* cases:
	 * 1. Total number of pre-selfatari nakade stones must
	 *    be 5 or smaller. (See above for that.)
	 * 2. If the selfatari is 8-touching all nakade stones,
	 *    it is proper nakade.
	 * 3. Otherwise, there must be only a single nakade
	 *    group, it must be at least 4-stone and its other
	 *    liberty must be 8-touching the same number of
	 *    stones as us. */
	int touch8 = neighbor_count_at(b, to, color);
	foreach_diag_neighbor(b, to, {
		if (board_at(b, c) != color) continue;
		/* Consider only internal stones. Otherwise, e.g.
		 * X O . X
		 * X . O X  can make trouble, bottom O is
		 * O X X X  irrelevant. */
		if (group_lib(b, group_at(b, c), 0) == to ||
		    group_lib(b, group_at(b, c), 1) == to)
			touch8++;
	});
	if (touch8 == stones)
		return true;

	if (s->groupcts[color] > 1)
		return false;
	if (stones == 3)   // 4 stones and self-atari not 8-connected to all of them -> living shape
		return false;
	if (stones < 3)    // always dead shape
		return true;
	
	int ltouch8 = neighbor_count_at(b, lib2, color);
	foreach_diag_neighbor(b, lib2, {
		if (board_at(b, c) != color) continue;
		if (group_lib(b, group_at(b, c), 0) == to ||
		    group_lib(b, group_at(b, c), 1) == to)
			ltouch8++;
	});
	return ltouch8 == touch8;
}
#endif

/* Return number of stones in resulting group after playing move.
 * @max_stones: max number of stones we care about */
static int
selfatari_group_stones(board_t *b, enum stone color, selfatari_state_t *s, int max_stones)
{
	int stones = 1;  /* Selfatari move stone */
	for (int i = 0; i < s->groupcts[color] && stones < max_stones; i++) {
		group_t g = s->groupids[color][i];
		stones += group_stone_count(b, g, max_stones);
	}
	return stones;
}

/* Find opponent group other liberty if we are putting it in atari. */
#define find_opponent_lib2(b, color, s)  \
	enum stone other_color = stone_other(color); \
	lib2 = pass; \
	for (int i = 0; i < s->groupcts[other_color]; i++) { \
		group_t g = s->groupids[other_color][i]; \
		if (group_libs(b, g) != 2) \
			continue; \
		\
		coord_t this_lib2 = group_other_lib(b, g, to); \
		if (is_pass(lib2)) \
			lib2 = this_lib2; \
		else if (this_lib2 != lib2) { \
			/* If we have two neighboring groups that do
			 * not share the other liberty, this for sure
			 * is not a good nakade. */ \
			return -1; \
		} \
	}

/* More complex throw-in, or in-progress capture from
 * the inside - we are in one of several situations:
 * a O O O O X  b O O O X  c O O O X  d O O O O O
 *   O . X . O    O X . .    O . X .    O . X . O
 *   # # # # #    # # # #    # # # #    # # # # #
 * Throw-ins have been taken care of in check_throwin(),
 * so it's either b or d now:
 * - b is desirable here (since maybe O has no backup two eyes)
 * - d is desirable if putting group in atari (otherwise we
 *   would never capture a single-eyed group). */
#define check_throw_in_or_inside_capture(b, color, to, s, capturing)			\
	if (s->groupcts[color] == 1 && group_is_onestone(b, s->groupids[color][0])) {	\
		group_t g2 = s->groupids[color][0];					\
		assert(group_libs(b, g2) <= 2);						\
		if (group_libs(b, g2) == 1)						\
			return false;  /*  b */						\
		return !capturing;							\
	}

#define check_throw_in(b, color, to, s)							\
	if (s->groupcts[color] == 1 && group_is_onestone(b, s->groupids[color][0])) {	\
		group_t g2 = s->groupids[color][0];					\
		if (group_libs(b, g2) == 1)						\
			return false;  /*  b */						\
	}

/* Estimate opponent liberties: group with most liberties */
static int
opponent_libs(board_t *b, enum stone color, selfatari_state_t *s)
{
	enum stone other_color = stone_other(color);

	int libs = 0;
	for (int i = 0; i < s->groupcts[other_color]; i++)
		libs = MAX(libs, group_libs(b, s->groupids[other_color][i]));
	return libs;
}

/* Check one-point eye / two-point eye / filled eye */
static bool
is_an_eye(board_t *b, coord_t coord, enum stone color)
{
	enum stone other_color = stone_other(color);

	if (board_at(b, coord) == S_NONE) {
		if (board_is_eyelike(b, coord, color))
			return board_is_one_point_eye(b, coord, color);

		/* Bigger eye candidate ? */
		if (neighbor_count_at(b, coord, color) + neighbor_count_at(b, coord, S_OFFBOARD) != 3)
			return false;

		coord_t lib = pass;
		foreach_neighbor(b, coord, {
			if (board_at(b, c) == S_NONE) {  lib = c; continue;  }
			group_t g = group_at(b, c);
			/* Eye stones in atari ? Not an eye. */
			if (board_at(b, c) == color && group_libs(b, g) == 1)
				return false;
			/* Eye filled with prisoners (= 1lib here) ok, otherwise not an eye. */
			if (board_at(b, c) == other_color && group_libs(b, g) != 1)
				return false;
		});

		if (!is_pass(lib)) {
			/* Must be somewhat surrounded. */
			return (neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) >= 2);
		}
		return true;
	}

	/* Opponent stone ? Ok if prisoner. */
	if (board_at(b, coord) == other_color) {
		group_t g = group_at(b, coord);
		if (group_libs(b, g) != 1)
			return false;

		coord_t lib = group_lib(b, g, 0);
		return !board_is_valid_play_no_suicide(b, other_color, lib);
		//return is_selfatari(b, other_color, lib);
	}

	return false;
}

#define bad_approach_move(b, color, c)  (!board_is_valid_play(b, color, c) || board_is_one_point_eye(b, c, color) || \
					 is_selfatari(b, color, c) || is_3lib_selfatari(b, color, c))

static bool
bad_noatari_single_throwin(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(!is_pass(s->lib));
#endif
	if (opponent_libs(b, color, s) > 3)
		return true;

	enum stone other_color = stone_other(color);

	if (immediate_liberty_count(b, s->lib) > 1 ||		/* Steal eye instead */
	    neighbor_count_at(b, s->lib, other_color) < 2 ||	/* Playing in tiger mouth */
	    neighbor_count_at(b, s->lib, color) || 		/* Making 2 nakade groups */
	    check_throwin(b, color, s->lib, 0) == false)	/* Playing next to a good throwin */
		return true;

	foreach_diag_neighbor(b, to, {
		if ((board_at(b, c) == S_NONE && !bad_approach_move(b, color, c)) ||
		    is_an_eye(b, c, other_color))
			return true;
	});
	foreach_diag_neighbor(b, s->lib, {
		if ((board_at(b, c) == S_NONE && !bad_approach_move(b, color, c)) ||
		    is_an_eye(b, c, other_color))
			return true;
	});
	return false;
}

static bool
bad_noatari_multiple_throwin(board_t *b, enum stone color, coord_t to, selfatari_state_t *s, int *pstones)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(s->groupcts[color] == 1);
#endif
	group_t g = s->groupids[color][0];
	int stones = *pstones = 1 + group_stone_count(b, g, 3);

	/* 2 or 3 stones throw-in ? */
	if (stones <= 3 &&
	    (is_pass(s->lib) || stones == 3) &&
	    group_libs(b, g) == 2) {	/* Not already in atari */

		/* Opponent group has more than 4 libs ? */
		if (opponent_libs(b, color, s) > 4)
			return true;

		coord_t other = group_other_lib(b, g, to);
		enum stone other_color = stone_other(color);

		foreach_diag_neighbor(b, to, {
			if (c == other) continue;
			if ((board_at(b, c) == S_NONE && !bad_approach_move(b, color, c)) ||
			    is_an_eye(b, c, other_color))
				return true;
		});
		foreach_diag_neighbor(b, other, {
			if (c == to) continue;
			if ((board_at(b, c) == S_NONE && !bad_approach_move(b, color, c)) ||
			    is_an_eye(b, c, other_color))
				return true;
		});
		
		/* 2-stones throw-in:
		 * Don't nakade if the other liberty is a good throw-in. */
		if (stones == 2 &&
		    check_throwin(b, color, other, g) == false)
			return true;
	}
	
	return false;
}

/* There is another possibility - we can self-atari if it is
 * a nakade: we put an enemy group in atari from the inside.
 * This branch also allows eyes falsification:
 * O O O . .  (This is different from throw-in to false eye
 * X X O O .  checked below in that there is no X stone at the
 * X . X O .  right of the star point in this diagram.)
 * X X X O O
 * X O * . . 
 * We also allow to only nakade if the created shape is dead
 * (http://senseis.xmp.net/?Nakade). */
static int
setup_nakade(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
	/* Single-stone throw-in should have one liberty, captures have been checked. */
	if (!s->groupcts[color])
		assert(sane_coord(s->lib));
#endif
	/* Look at the enemy groups and determine the other contended
	 * liberty. We must make sure the liberty:
	 * (i) is an internal liberty
	 * (ii) filling it to capture our group will not gain safety */
	coord_t lib2;
	find_opponent_lib2(b, color, s);

	if (is_pass(lib2)) {
	        /* Not putting any group in atari.
		 * Could be creating dead shape / helping semeai though.
		 * Semeai test is too expensive, relax checks to let potential good
		 * moves through and try to filter out as many bad moves as possible. */

		/* Simple throw-in ? */
		if (!s->groupcts[color])
			return bad_noatari_single_throwin(b, color, to, s);

		/* Before checking if it's a useful nakade
		 * make sure it can't connect out ! */
		if (can_escape_instead(b, color, to, s))
			return -1;

		/* Throw-in to steal eye is ok. */
		check_throw_in(b, color, to, s);

		/* Handle 2-stones throw-ins (and some 3-stones ones). */
		if (s->groupcts[color] == 1) {
			int stones;
			if (bad_noatari_multiple_throwin(b, color, to, s, &stones))
				return true;
			if (stones == 2)
				return false;
		}

		/* No good nakade above 6 stones. */
		int stones = selfatari_group_stones(b, color, s, 7);
		if (stones > 6)   return true;

		return !nakade_making_dead_shape(b, color, to, s, stones);
	}

	/* Check other liberty is internal.
	 * If so it also can't escape (we would have 2 libs then). */
	if (is_bad_nakade(b, color, to, lib2, s))
		return -1;

	/* Now, we must distinguish between nakade and eye
	 * falsification; moreover, we must not falsify an eye
	 * by more than two stones. */

	if (s->groupcts[color] < 1)
		return false;  /* Simple throw-in, an easy case */
	
	check_throw_in_or_inside_capture(b, color, to, s, true);

	/* No good nakade above 6 stones. */
	int stones = selfatari_group_stones(b, color, s, 7);
	if (stones > 6)
		return true;
	
	return !nakade_making_dead_shape(b, color, to, s, stones);
}

static int
setup_nakade_big_group_only(board_t *b, enum stone color, coord_t to, selfatari_state_t *s)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
#endif
	// Throwing a single stone in ? Fine.
	if (!s->groupcts[color])
		return false;
	
	/* Before checking if it's a useful nakade
	 * make sure it can't connect out ! */
	if (can_escape_instead(b, color, to, s))
		return true;
	
	/* Creating a 2-stone group ? Fine too */
	/* Could still be really stupid though
	 * (if can countercapture instead for ex) */
	if (s->groupcts[color] == 1 && group_is_onestone(b, s->groupids[color][0]))
		return false;
	
	/* Making 3-stone group or more */	

	// XXX Not in setup_nakade() !
	//     Makes is_bad_selfatari() and is_really_bad_selfatari() disagree
	//     which cannot be good. Removing it here or adding it there is
	//     pretty bad for playouts balance though, investigate ...
	/* Not always true but for the most part, if we're creating
	 * a 3+ stone group in atari and we could have captured
	 * something instead it's really stupid, even if shape is
	 * dead locally. */
	for (int j = 0; j < s->groupcts[color]; j++) {
		group_t g2 = s->groupids[color][j];
		if (can_countercapture(b, g2, NULL))
			return true;
	}

	/* No good nakade above 6 stones. */
	int stones = selfatari_group_stones(b, color, s, 7);
	if (stones > 6)
		return true;

	/* Look at the enemy groups and determine the other contended
	 * liberty. We must make sure the liberty:
	 * (i) is an internal liberty
	 * (ii) filling it to capture our group will not gain safety */
	coord_t lib2;
	find_opponent_lib2(b, color, s);

	if (!is_pass(lib2))
		if (is_bad_nakade(b, color, to, lib2, s))
			return -1;

	return !nakade_making_dead_shape(b, color, to, s, stones);
}


/**********************************************************************************/
/* Public API */

/* For testing purposes mostly.
 * Only does the 3lib suicide check of is_bad_selfatari(). */
bool
is_3lib_selfatari(board_t *b, enum stone color, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
#endif
	selfatari_state_t s;
	init_selfatari_state(b, color, to, &s);

	for (int i = 0; i < s.groupcts[color]; i++) {
		group_t g = s.groupids[color][i];
		if (group_libs(b, g) == 3 &&
		    three_liberty_suicide(b, g, color, to, &s))
			return true;
	}
	return false;
}

/* Check if move sets up a snapback.
 * Only does the check_snapback() part of is_bad_selfatari(). */
bool
is_snapback(board_t *b, enum stone color, coord_t to, group_t *snapback_group)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
#endif
	if (immediate_liberty_count(b, to) != 1 ||
	    neighbor_count_at(b, to, color) ||			  // fast examine_friendly_groups()
	    board_get_atari_neighbor(b, to, stone_other(color)))  // fast examine_enemy_groups()
		return false;

	selfatari_state_t s;
	init_selfatari_state(b, color, to, &s);

	if (check_snapback(b, color, to, &s) == false) {
		if (snapback_group)
			*snapback_group = s.snapback_group;
		return true;
	}
	return false;
}

bool
is_bad_selfatari_slow(board_t *b, enum stone color, coord_t to, int flags)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
#endif
	if (DEBUGL(6))
		fprintf(stderr, "sar check %s %s\n", stone2str(color), coord2sstr(to));
	/* Assess if we actually gain any liberties by this escape route.
	 * Note that this is not 100% as we cannot check whether we are
	 * connecting out or just to ourselves. */

	selfatari_state_t s;
	init_selfatari_state(b, color, to, &s);

	int d;
	d = examine_friendly_groups(b, color, to, &s, flags);
	if (d >= 0)     return d;
	if (DEBUGL(6))  fprintf(stderr, "no friendly group\n");
	
	d = examine_enemy_groups(b, color, to, &s);
	if (d >= 0)	return d;
	if (DEBUGL(6))  fprintf(stderr, "no capture\n");

	d = check_snapback(b, color, to, &s);
	if (d >= 0)	return d;
	if (DEBUGL(6))  fprintf(stderr, "no snapback\n");
	
	if (!(flags & SELFATARI_BIG_GROUPS_ONLY)) {
		group_t g = (s.groupcts[color] ? s.groupids[color][0] : 0);
		d = check_throwin(b, color, to, g);
		if (d >= 0)	return d;
		if (DEBUGL(6))	fprintf(stderr, "no throw-in group\n");
	}	

	if (flags & SELFATARI_BIG_GROUPS_ONLY)
		d = setup_nakade_big_group_only(b, color, to, &s);
	else
		d = setup_nakade(b, color, to, &s);
	if (d >= 0)	return d;
	if (DEBUGL(6))	fprintf(stderr, "no nakade group\n");	

	/* No way to pull out, no way to connect out. This really
	 * is a bad self-atari! */
	return true;
}


coord_t
selfatari_cousin_approach_moves(board_t *b, enum stone color, coord_t coord, group_t *bygroup)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(coord));
	assert(board_at(b, coord) == S_NONE);
#endif
	group_t groups[4]; int groups_n = 0;
	foreach_neighbor(b, coord, {
		/* Consider own groups only. */
		if (board_at(b, c) != color)  continue;
		group_t g = group_at(b, c);
		if (g && group_libs(b, g) == 2) {
			groups[groups_n++] = g;
		}
	});
	if (!groups_n)  return pass;

	int gn = fast_random(groups_n);
	int gl = gn;
	for (; gn - gl < groups_n; gn++) {
		int gnm = gn % groups_n;
		group_t group = groups[gnm];

		coord_t lib2;
		/* Can we get liberties by capturing a neighbor? */
		mq_t ccq;  mq_init(&ccq);
		if (can_countercapture(b, group, &ccq))
			lib2 = mq_pick(&ccq);
		else {
			lib2 = group_other_lib(b, group, coord);
			if (board_is_one_point_eye(b, lib2, color) ||
			    is_bad_selfatari(b, color, lib2))
				continue;
		}
		*bygroup = group;
		return lib2;
	}
	return pass;
}

coord_t
selfatari_cousin(board_t *b, enum stone color, coord_t coord)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(coord));
	assert(board_at(b, coord) == S_NONE);
#endif
	group_t groups[4]; int groups_n = 0;
	int groupsbycolor[4] = {0, 0, 0, 0};
	if (DEBUGL(6))
		fprintf(stderr, "cousin group search: ");
	foreach_neighbor(b, coord, {
		enum stone s = board_at(b, c);
		group_t g = group_at(b, c);
		if (g && group_libs(b, g) == 2) {
			groups[groups_n++] = g;
			groupsbycolor[s]++;
			if (DEBUGL(6))
				fprintf(stderr, "%s(%s) ", coord2sstr(c), stone2str(s));
		}
	});
	if (DEBUGL(6))
		fprintf(stderr, "\n");

	if (!groups_n)
		return pass;

	int gn;
	enum stone other_color = stone_other(color);
	if (groupsbycolor[other_color]) {
		/* Prefer to fill the other liberty of an opponent
		 * group to filling own approach liberties. */
		int gl = fast_random(groups_n);
		for (gn = gl; gn < groups_n; gn++)
			if (board_at(b, groups[gn]) == other_color)
				goto found;
		for (gn = 0; gn < gl; gn++)
			if (board_at(b, groups[gn]) == other_color)
				goto found;
found:;
	} else {
		gn = fast_random(groups_n);
	}
	int gl = gn;
	for (; gn - gl < groups_n; gn++) {
		int gnm = gn % groups_n;
		group_t group = groups[gnm];

		coord_t lib2;
		/* Can we get liberties by capturing a neighbor? */
		mq_t ccq;  mq_init(&ccq);
		if (board_at(b, group) == color &&
		    can_countercapture(b, group, &ccq)) {
			lib2 = mq_pick(&ccq);

		} else {
			lib2 = group_other_lib(b, group, coord);
			if (board_is_one_point_eye(b, lib2, board_at(b, group)))
				continue;
			if (is_bad_selfatari(b, color, lib2))
				continue;
		}
		return lib2;
	}
	return pass;
}
