#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "tactics/2lib.h"
#include "tactics/selfatari.h"


/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


static bool
miai_2lib(struct board *b, group_t group, enum stone color)
{
	bool can_connect = false, can_pull_out = false;
	/* We have miai if we can either connect on both libs,
	 * or connect on one lib and escape on another. (Just
	 * having two escape routes can be risky.) We must make
	 * sure that we don't consider following as miai:
	 * X X X O
	 * X . . O
	 * O O X O - left dot would be pull-out, right dot connect */
	foreach_neighbor(b, board_group_info(b, group).lib[0], {
		enum stone cc = board_at(b, c);
		if (cc == S_NONE && cc != board_at(b, board_group_info(b, group).lib[1])) {
			can_pull_out = true;
		} else if (cc != color) {
			continue;
		}

		group_t cg = group_at(b, c);
		if (cg && cg != group && board_group_info(b, cg).libs > 1)
			can_connect = true;
	});
	foreach_neighbor(b, board_group_info(b, group).lib[1], {
		enum stone cc = board_at(b, c);
		if (c == board_group_info(b, group).lib[0])
			continue;
		if (cc == S_NONE && can_connect) {
			return true;
		} else if (cc != color) {
			continue;
		}

		group_t cg = group_at(b, c);
		if (cg && cg != group && board_group_info(b, cg).libs > 1)
			return (can_connect || can_pull_out);
	});
	return false;
}

void
check_group_atari(struct board *b, group_t group, enum stone owner,
		  enum stone to_play, struct move_queue *q, int tag)
{
	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, group).lib[i];
		assert(board_at(b, lib) == S_NONE);
		if (!board_is_valid_play(b, to_play, lib))
			continue;

		/* Don't play at the spot if it is extremely short
		 * of liberties... */
		/* XXX: This looks harmful, could significantly
		 * prefer atari to throwin:
		 *
		 * XXXOOOOOXX
		 * .OO.....OX
		 * XXXOOOOOOX */
#if 0
		if (neighbor_count_at(b, lib, stone_other(owner)) + immediate_liberty_count(b, lib) < 2)
			continue;
#endif

		/* If the move is too "lumpy", do not play it:
		 *
		 * #######
		 * ..O.X.X <- always play the left one!
		 * OXXXXXX */
		if (neighbor_count_at(b, lib, stone_other(owner)) + neighbor_count_at(b, lib, S_OFFBOARD) == 3)
			continue;

		/* XXX: We do not check connecting to a short-on-liberty
		 * group. */

		/* If we are the defender, do not escape with moves
		 * that do not gain liberties anyway since one of the
		 * "gained" liberties is shared. */
		if (to_play == owner
		    && neighbor_count_at(b, lib, stone_other(owner)) + neighbor_count_at(b, lib, S_OFFBOARD) == 2
		    && coord_is_adjecent(lib, board_group_info(b, group).lib[1 - i], b))
			continue;

#ifdef NO_DOOMED_GROUPS
		/* If the owner can't play at the spot, we don't want
		 * to bother either. */
		if (is_bad_selfatari(b, owner, lib))
			continue;
#endif

		/* Of course we don't want to play bad selfatari
		 * ourselves, if we are the attacker... */
		if (
#ifdef NO_DOOMED_GROUPS
		    to_play != owner &&
#endif
		    is_bad_selfatari(b, to_play, lib))
			continue;

		/* Tasty! Crispy! Good! */
		mq_add(q, lib, tag);
		mq_nodup(q);
	}
}

void
group_2lib_check(struct board *b, group_t group, enum stone to_play, struct move_queue *q, int tag)
{
	enum stone color = board_at(b, group_base(group));
	assert(color != S_OFFBOARD && color != S_NONE);

	if (DEBUGL(5))
		fprintf(stderr, "[%s] 2lib check of color %d\n",
			coord2sstr(group, b), color);

	/* Do not try to atari groups that cannot be harmed. */
	if (miai_2lib(b, group, color))
		return;

	check_group_atari(b, group, color, to_play, q, tag);

	/* Can we counter-atari another group, if we are the defender? */
	if (to_play != color)
		return;
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != stone_other(color))
				continue;
			group_t g2 = group_at(b, c);
			if (board_group_info(b, g2).libs == 1) {
				/* We can capture a neighbor. */
				mq_add(q, board_group_info(b, g2).lib[0], tag);
				mq_nodup(q);
				continue;
			}
			if (board_group_info(b, g2).libs != 2)
				continue;
			check_group_atari(b, g2, color, to_play, q, tag);
		});
	} foreach_in_group_end;
}
