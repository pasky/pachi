#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"


bool
is_bad_selfatari_slow(struct board *b, enum stone color, coord_t to)
{
	//fprintf(stderr, "sar check %s %s\n", stone2str(color), coord2sstr(to, b));
	/* Assess if we actually gain any liberties by this escape route.
	 * Note that this is not 100% as we cannot check whether we are
	 * connecting out or just to ourselves. */
	int groupcts[S_MAX] = {};
	group_t groupids[S_MAX][4] = {};
	foreach_neighbor(b, to, {
		enum stone s = board_at(b, c);
		groupids[s][groupcts[s]++] = group_at(b, c);
	});

	/* We have shortage of liberties; that's the point. */
	assert(groupcts[S_NONE] <= 1);

	/* This is set if this move puts a group out of _all_
	 * liberties; we need to watch out for snapback then. */
	bool friend_has_no_libs = false;
	/* We may have one liberty, but be looking for one more.
	 * In that case, @needs_more_lib is id of group
	 * already providing one, don't consider it again. */
	group_t needs_more_lib = 0;
	/* ID of the first liberty, providing it again is not
	 * interesting. */
	coord_t needs_more_lib_except = 0;

	/* Examine friendly groups: */
	for (int i = 0; i < 4; i++) {
		/* We can escape by connecting to this group if it's
		 * not in atari. */
		group_t g = groupids[color][i];
		if (!g) continue;

		if (board_group_info(b, g).libs == 1) {
			if (!needs_more_lib)
				friend_has_no_libs = true;
			// or we already have a friend with 1 lib
			continue;
		}

		/* Could we self-atari the group here? */
		if (board_group_info(b, g).libs > 2)
			return false;

		/* We need to have another liberty, and
		 * it must not be the other liberty of
		 * the group. */
		int lib2 = board_group_info(b, g).lib[0];
		if (lib2 == to) lib2 = board_group_info(b, g).lib[1];
		/* Maybe we already looked at another
		 * group providing one liberty? */
		if (needs_more_lib && needs_more_lib != g
		    && needs_more_lib_except != lib2)
			return false;

		/* Can we get the liberty locally? */
		/* Yes if we are route to more liberties... */
		if (groupcts[S_NONE] > 1)
			return false;
		/* ...or one liberty, but not lib2. */
		if (groupcts[S_NONE] > 0
		    && !coord_is_adjecent(lib2, to, b))
			return false;

		/* ...ok, then we can still contribute a liberty
		 * later by capturing something. */
		needs_more_lib = g;
		needs_more_lib_except = lib2;
		friend_has_no_libs = false;
	}

	//fprintf(stderr, "no friendly group\n");

	/* We may be able to gain a liberty by capturing this group. */
	group_t can_capture = 0;

	/* Examine enemy groups: */
	for (int i = 0; i < 4; i++) {
		/* We can escape by capturing this group if it's in atari. */
		group_t g = groupids[stone_other(color)][i];
		if (!g || board_group_info(b, g).libs > 1)
			continue;

		/* But we need to get to at least two liberties by this;
		 * we already have one outside liberty, or the group is
		 * more than 1 stone (in that case, capturing is always
		 * nice!). */
		if (groupcts[S_NONE] > 0 || !group_is_onestone(b, g))
			return false;
		/* ...or, it's a ko stone, */
		if (neighbor_count_at(b, g, color) + neighbor_count_at(b, g, S_OFFBOARD) == 3) {
			/* and we don't have a group to save: then, just taking
			 * single stone means snapback! */
			if (!friend_has_no_libs)
				return false;
		}
		/* ...or, we already have one indirect liberty provided
		 * by another group. */
		if (needs_more_lib || (can_capture && can_capture != g))
			return false;
		can_capture = g;

	}

	//fprintf(stderr, "no cap group\n");

	if (!needs_more_lib && !can_capture && !groupcts[S_NONE]) {
		/* We have no hope for more fancy tactics - this move is simply
		 * a suicide, not even a self-atari. */
		//fprintf(stderr, "suicide\n");
		return true;
	}
	/* XXX: I wonder if it makes sense to continue if we actually
	 * just !needs_more_lib. */

	/* There is another possibility - we can self-atari if it is
	 * a nakade: we put an enemy group in atari from the inside. */
	/* This branch also allows eyes falsification:
	 * O O O . .  (This is different from throw-in to false eye
	 * X X O O .  checked below in that there is no X stone at the
	 * X . X O .  right of the star point in this diagram.)
	 * X X X O O
	 * X O * . . */
	/* TODO: Allow to only nakade if the created shape is dead
	 * (http://senseis.xmp.net/?Nakade). */

	/* This branch also covers snapback, which is kind of special
	 * nakade case. ;-) */
	for (int i = 0; i < 4; i++) {
		group_t g = groupids[stone_other(color)][i];
		if (!g || board_group_info(b, g).libs != 2)
			continue;
		/* Simple check not to re-examine the same group. */
		if (i > 0 && groupids[stone_other(color)][i] == groupids[stone_other(color)][i - 1])
			continue;

		/* We must make sure the other liberty of that group:
		 * (i) is an internal liberty
		 * (ii) filling it to capture our group will not gain
		 * safety */

		/* Let's look at the other liberty neighbors: */
		int lib2 = board_group_info(b, g).lib[0];
		if (lib2 == to) lib2 = board_group_info(b, g).lib[1];
		foreach_neighbor(b, lib2, {
			/* This neighbor of course does not contribute
			 * anything to the enemy. */
			if (board_at(b, c) == S_OFFBOARD)
				continue;

			/* If the other liberty has empty neighbor,
			 * it must be the original liberty; otherwise,
			 * since the whole group has only 2 liberties,
			 * the other liberty may not be internal and
			 * we are nakade'ing eyeless group from outside,
			 * which is stupid. */
			if (board_at(b, c) == S_NONE) {
				if (c == to)
					continue;
				else
					goto invalid_nakade;
			}

			int g2 = group_at(b, c);
			/* If the neighbor is of our color, it must
			 * be our group; if it is a different group,
			 * it must not be in atari. */
			/* X X X X  We will not allow play on 'a',
			 * X X a X  because 'b' would capture two
			 * X O b X  different groups, forming two
			 * X X X X  eyes. */
			if (board_at(b, c) == color) {
				if (board_group_info(b, group_at(b, c)).libs > 1)
					continue;
				/* Our group == one of the groups
				 * we (@to) are connected to. */
				int j;
				for (j = 0; j < 4; j++)
					if (groupids[color][j] == g2)
						break;
				if (j == 4)
					goto invalid_nakade;
				continue;
			}

			/* The neighbor is enemy color. It's ok if
			 * it's still the same group or this is its
			 * only liberty. */
			if (g == g2 || board_group_info(b, g2).libs == 1)
				continue;
			/* Otherwise, it must have the exact same
			 * liberties as the original enemy group. */
			if (board_group_info(b, g2).libs == 2
			    && (board_group_info(b, g2).lib[0] == to
			        || board_group_info(b, g2).lib[1] == to))
				continue;

			goto invalid_nakade;
		});

		/* Now, we must distinguish between nakade and eye
		 * falsification; we must not falsify an eye by more
		 * than two stones. */
		if (groupcts[color] < 1 ||
		    (groupcts[color] == 1 && group_is_onestone(b, groupids[color][0])))
			return false;

		/* We would create more than 2-stone group; in that
		 * case, the liberty of our result must be lib2,
		 * indicating this really is a nakade. */
		for (int j = 0; j < 4; j++) {
			group_t g2 = groupids[color][j];
			if (!g2) continue;
			assert(board_group_info(b, g2).libs <= 2);
			if (board_group_info(b, g2).libs == 2) {
				if (board_group_info(b, g2).lib[0] != lib2
				    && board_group_info(b, g2).lib[1] != lib2)
					goto invalid_nakade;
			} else {
				assert(board_group_info(b, g2).lib[0] == to);
			}
		}

		return false;

invalid_nakade:;
	}

	//fprintf(stderr, "no nakade group\n");

	/* We can be throwing-in to false eye:
	 * X X X O X X X O X X X X X
	 * X . * X * O . X * O O . X
	 * # # # # # # # # # # # # # */
	/* We cannot sensibly throw-in into a corner. */
	if (neighbor_count_at(b, to, S_OFFBOARD) < 2
	    && neighbor_count_at(b, to, stone_other(color))
	       + neighbor_count_at(b, to, S_OFFBOARD) == 3
	    && board_is_false_eyelike(b, &to, stone_other(color))) {
		assert(groupcts[color] <= 1);
		/* Single-stone throw-in may be ok... */
		if (groupcts[color] == 0) {
			/* O X .  There is one problem - when it's
			 * . * X  actually not a throw-in!
			 * # # #  */
			foreach_neighbor(b, to, {
				if (board_at(b, c) == S_NONE) {
					/* Is the empty neighbor an escape path? */
					/* (Note that one S_NONE neighbor is already @to.) */
					if (neighbor_count_at(b, c, stone_other(color))
					    + neighbor_count_at(b, c, S_OFFBOARD) < 2)
						goto invalid_throwin;
				}
			});
			return false;
		}

		/* Multi-stone throwin...? */
		assert(groupcts[color] == 1);
		group_t g = groupids[color][0];

		assert(board_group_info(b, g).libs <= 2);
		/* Suicide is definitely NOT ok, no matter what else
		 * we could test. */
		if (board_group_info(b, g).libs == 1)
			return true;

		/* In that case, we must be connected to at most one stone,
		 * or throwin will not destroy any eyes. */
		if (group_is_onestone(b, g))
			return false;
invalid_throwin:;
	}

	//fprintf(stderr, "no throw-in group\n");

	/* No way to pull out, no way to connect out. This really
	 * is a bad self-atari! */
	return true;
}


bool
board_stone_radar(struct board *b, coord_t coord, int distance)
{
	int bounds[4] = {
		coord_x(coord, b) - distance,
		coord_y(coord, b) - distance,
		coord_x(coord, b) + distance,
		coord_y(coord, b) + distance
	};
	for (int i = 0; i < 4; i++)
		if (bounds[i] < 1)
			bounds[i] = 1;
		else if (bounds[i] > board_size(b) - 2)
			bounds[i] = board_size(b) - 2;
	for (int x = bounds[0]; x <= bounds[2]; x++)
		for (int y = bounds[1]; y <= bounds[3]; y++)
			if (board_atxy(b, x, y) != S_NONE) {
				/* fprintf(stderr, "radar %d,%d,%d: %d,%d (%d)\n",
					coord_x(coord, b), coord_y(coord, b),
					distance, x, y, board_atxy(b, x, y)); */
				return true;
			}
	return false;
}
