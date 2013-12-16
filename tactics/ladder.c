#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "mq.h"
#include "tactics/1lib.h"
#include "tactics/ladder.h"


bool
is_border_ladder(struct board *b, coord_t coord, enum stone lcolor)
{
	int x = coord_x(coord, b), y = coord_y(coord, b);

	if (DEBUGL(5))
		fprintf(stderr, "border ladder\n");
	/* Direction along border; xd is horiz. border, yd vertical. */
	int xd = 0, yd = 0;
	if (board_atxy(b, x + 1, y) == S_OFFBOARD || board_atxy(b, x - 1, y) == S_OFFBOARD)
		yd = 1;
	else
		xd = 1;
	/* Direction from the border; -1 is above/left, 1 is below/right. */
	int dd = (board_atxy(b, x + yd, y + xd) == S_OFFBOARD) ? 1 : -1;
	if (DEBUGL(6))
		fprintf(stderr, "xd %d yd %d dd %d\n", xd, yd, dd);
	/* | ? ?
	 * | . O #
	 * | c X #
	 * | . O #
	 * | ? ?   */
	/* This is normally caught, unless we have friends both above
	 * and below... */
	if (board_atxy(b, x + xd * 2, y + yd * 2) == lcolor
	    && board_atxy(b, x - xd * 2, y - yd * 2) == lcolor)
		return false;

	/* ...or can't block where we need because of shortage
	 * of liberties. */
	group_t g1 = group_atxy(b, x + xd - yd * dd, y + yd - xd * dd);
	int libs1 = board_group_info(b, g1).libs;
	group_t g2 = group_atxy(b, x - xd - yd * dd, y - yd - xd * dd);
	int libs2 = board_group_info(b, g2).libs;
	if (DEBUGL(6))
		fprintf(stderr, "libs1 %d libs2 %d\n", libs1, libs2);
	/* Already in atari? */
	if (libs1 < 2 || libs2 < 2)
		return false;
	/* Would be self-atari? */
	if (libs1 < 3 && (board_atxy(b, x + xd * 2, y + yd * 2) != S_NONE
			  || coord_is_adjecent(board_group_info(b, g1).lib[0], board_group_info(b, g1).lib[1], b)))
		return false;
	if (libs2 < 3 && (board_atxy(b, x - xd * 2, y - yd * 2) != S_NONE
			  || coord_is_adjecent(board_group_info(b, g2).lib[0], board_group_info(b, g2).lib[1], b)))
		return false;
	return true;
}


/* This is a rather expensive ladder reader. It can read out any sequences
 * where laddered group should be kept at two liberties. The recursion
 * always makes a "to-be-laddered" move and then considers the chaser's
 * two alternatives (usually, one of them is trivially refutable). The
 * function returns true if there is a branch that ends up with laddered
 * group captured, false if not (i.e. for each branch, laddered group can
 * gain three liberties). */

static bool
middle_ladder_walk(struct board *b, struct board *bset, group_t laddered, coord_t nextmove, enum stone lcolor)
{
	assert(board_group_info(b, laddered).libs == 1);

	/* First, escape. */
	if (DEBUGL(6))
		fprintf(stderr, "  ladder escape %s\n", coord2sstr(nextmove, b));
	struct move m = { nextmove, lcolor };
	int res = board_play(b, &m);
	laddered = group_at(b, laddered);
	assert(res >= 0);
	if (DEBUGL(8)) {
		board_print(b, stderr);
		fprintf(stderr, "%s c %d\n", coord2sstr(laddered, b), board_group_info(b, laddered).libs);
	}

	if (board_group_info(b, laddered).libs == 1) {
		if (DEBUGL(6))
			fprintf(stderr, "* we can capture now\n");
		return true;
	}
	if (board_group_info(b, laddered).libs > 2) {
		if (DEBUGL(6))
			fprintf(stderr, "* we are free now\n");
		return false;
	}

	foreach_neighbor(b, m.coord, {
		if (board_at(b, c) == stone_other(lcolor) && board_group_info(b, group_at(b, c)).libs == 1) {
			/* We can capture one of the ladder stones
			 * anytime later. */
			/* XXX: If we were very lucky, capturing
			 * this stone will not help us escape.
			 * That should be pretty rate. */
			if (DEBUGL(6))
				fprintf(stderr, "* can capture chaser\n");
			return false;
		}
	});

	/* Now, consider alternatives. */
	int liblist[2], libs = 0;
	for (int i = 0; i < 2; i++) {
		coord_t ataristone = board_group_info(b, laddered).lib[i];
		coord_t escape = board_group_info(b, laddered).lib[1 - i];
		if (immediate_liberty_count(b, escape) > 2 + coord_is_adjecent(ataristone, escape, b)) {
			/* Too much free space, ignore. */
			continue;
		}
		liblist[libs++] = i;
	}

	/* Try out the alternatives. */
	bool is_ladder = false;
	for (int i = 0; !is_ladder && i < libs; i++) {
		struct board *b2 = b;
		if (i != libs - 1) {
			b2 = bset++;
			board_copy(b2, b);
		}

		coord_t ataristone = board_group_info(b2, laddered).lib[liblist[i]];
		// coord_t escape = board_group_info(b2, laddered).lib[1 - liblist[i]];
		struct move m = { ataristone, stone_other(lcolor) };
		int res = board_play(b2, &m);
		/* If we just played self-atari, abandon ship. */
		/* XXX: If we were very lucky, capturing this stone will
		 * not help us escape. That should be pretty rate. */
		if (DEBUGL(6))
			fprintf(stderr, "(%d=%d) ladder atari %s (%d libs)\n", i, res, coord2sstr(ataristone, b2), board_group_info(b2, group_at(b2, ataristone)).libs);
		if (res >= 0 && board_group_info(b2, group_at(b2, ataristone)).libs > 1)
			is_ladder = middle_ladder_walk(b2, bset, laddered, board_group_info(b2, laddered).lib[0], lcolor);

		if (i != libs - 1) {
			board_done_noalloc(b2);
		}
	}
	if (DEBUGL(6))
		fprintf(stderr, "propagating %d\n", is_ladder);
	return is_ladder;
}

bool
is_middle_ladder(struct board *b, coord_t coord, group_t laddered, enum stone lcolor)
{
	/* TODO: Remove the redundant parameters. */
	assert(board_group_info(b, laddered).libs == 1);
	assert(board_group_info(b, laddered).lib[0] == coord);
	assert(board_at(b, laddered) == lcolor);

	/* If we can move into empty space or do not have enough space
	 * to escape, this is obviously not a ladder. */
	if (immediate_liberty_count(b, coord) != 2) {
		if (DEBUGL(5))
			fprintf(stderr, "no ladder, wrong free space\n");
		return false;
	}

	/* A fair chance for a ladder. Group in atari, with some but limited
	 * space to escape. Time for the expensive stuff - set up a temporary
	 * board and start selective 2-liberty search. */

	struct board *bset = malloc2(BOARD_MAX_SIZE * 2 * sizeof(struct board));

	struct move_queue ccq = { .moves = 0 };
	if (can_countercapture(b, lcolor, laddered, lcolor, &ccq, 0)) {
		/* We could escape by countercapturing a group.
		 * Investigate. */
		assert(ccq.moves > 0);
		for (unsigned int i = 0; i < ccq.moves; i++) {
			struct board b2;
			board_copy(&b2, b);
			bool is_ladder = middle_ladder_walk(&b2, bset, laddered, ccq.move[i], lcolor);
			board_done_noalloc(&b2);
			if (!is_ladder) {
				free(bset);
				return false;
			}
		}
	}

	struct board b2;
	board_copy(&b2, b);
	bool is_ladder = middle_ladder_walk(&b2, bset, laddered, board_group_info(&b2, laddered).lib[0], lcolor);
	board_done_noalloc(&b2);
	free(bset);
	return is_ladder;
}

bool
wouldbe_ladder(struct board *b, group_t group, coord_t escapelib, coord_t chaselib, enum stone lcolor)
{
	assert(board_group_info(b, group).libs == 2);
	assert(board_at(b, group) == lcolor);

	if (DEBUGL(6))
		fprintf(stderr, "would-be ladder check - does %s %s play out chasing move %s?\n",
			stone2str(lcolor), coord2sstr(escapelib, b), coord2sstr(chaselib, b));

	if (!coord_is_8adjecent(escapelib, chaselib, b)) {
		if (DEBUGL(5))
			fprintf(stderr, "cannot determine ladder for remote simulated stone\n");
		return false;
	}

	if (neighbor_count_at(b, chaselib, lcolor) != 1 || immediate_liberty_count(b, chaselib) != 2) {
		if (DEBUGL(5))
			fprintf(stderr, "overly trivial for a ladder\n");
		return false;
	}

	bool is_ladder = false;
	struct board *bset = malloc2(BOARD_MAX_SIZE * 2 * sizeof(struct board));
	struct board b2;
	board_copy(&b2, b);

	struct move m = { chaselib, stone_other(lcolor) };
	int res = board_play(&b2, &m);
	if (res >= 0)
		is_ladder = middle_ladder_walk(&b2, bset, group, board_group_info(&b2, group).lib[0], lcolor);

	board_done_noalloc(&b2);
	free(bset);
	return is_ladder;
}
