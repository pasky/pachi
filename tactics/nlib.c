#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "tactics/2lib.h"
#include "tactics/nlib.h"
#include "tactics/selfatari.h"


void
group_nlib_defense_check(struct board *b, group_t group, enum stone to_play, struct move_queue *q, int tag)
{
	enum stone color = to_play;
	assert(color != S_OFFBOARD && color != S_NONE
	       && color == board_at(b, group_base(group)));

	if (DEBUGL(5))
		fprintf(stderr, "[%s] nlib defense check of color %d\n",
			coord2sstr(group, b), color);

#if 0
	/* XXX: The code below is specific for 3-liberty groups. Its impact
	 * needs to be tested first, and possibly moved to a more appropriate
	 * place. */

	/* First, look at our liberties. */
	int continuous = 0, enemy = 0, spacy = 0, eyes = 0;
	for (int i = 0; i < 3; i++) {
		coord_t c = board_group_info(b, group).lib[i];
		eyes += board_is_one_point_eye(b, c, to_play);
		continuous += coord_is_adjecent(c, board_group_info(b, group).lib[(i + 1) % 3], b);
		enemy += neighbor_count_at(b, c, stone_other(color));
		spacy += immediate_liberty_count(b, c) > 1;
	}

	/* Safe groups are boring. */
	if (eyes > 1)
		return;

	/* If all our liberties are in single line and they are internal,
	 * this is likely a tiny three-point eyespace that we rather want
	 * to live at! */
	assert(continuous < 3);
	if (continuous == 2 && !enemy && spacy == 1) {
		assert(!eyes);
		int i;
		for (i = 0; i < 3; i++)
			if (immediate_liberty_count(b, board_group_info(b, group).lib[i]) == 2)
				break;
		/* Play at middle point. */
		mq_add(q, board_group_info(b, group).lib[i], tag);
		mq_nodup(q);
		return;
	}
#endif

	/* "Escaping" (gaining more liberties) with many-liberty group
	 * is difficult. Do not even try. */

	/* There is another way to gain safety - through winning semeai
	 * with another group. */
	/* We will not look at taking liberties of enemy n-groups, since
	 * we do not try to gain liberties for own n-groups. That would
	 * be really unbalanced (and most of our liberty-taking moves
	 * would be really stupid, most likely). */

	/* However, it is possible that we must start capturing a 2-lib
	 * neighbor right now, because of approach liberties. Therefore,
	 * we will check for this case. If we take a liberty of a group
	 * even if we could have waited another move, no big harm done
	 * either. */

	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != stone_other(color))
				continue;
			group_t g2 = group_at(b, c);
			if (board_group_info(b, g2).libs != 2)
				continue;
			can_atari_group(b, g2, stone_other(color), to_play, q, tag, true /* XXX */);
		});
	} foreach_in_group_end;
}
