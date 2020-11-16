#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

#define DEBUG
#include "board.h"
#include "board_undo.h"
#include "debug.h"
#include "mq.h"
#include "tactics/1lib.h"
#include "tactics/selfatari.h"
#include "tactics/dragon.h"
#include "tactics/ladder.h"


/* Read out middle ladder countercap sequences ? Otherwise we just
 * assume ladder doesn't work if countercapturing is possible. */
#define MIDDLE_LADDER_CHECK_COUNTERCAP 1


bool
is_border_ladder(board_t *b, group_t laddered)
{
	coord_t coord = board_group_info(b, laddered).lib[0];
	enum stone lcolor = board_at(b, group_base(laddered));
	
	if (can_countercapture(b, laddered, NULL))
		return false;
	
	int x = coord_x(coord), y = coord_y(coord);

	if (DEBUGL(5))  fprintf(stderr, "border ladder\n");
	
	/* Direction along border; xd is horiz. border, yd vertical. */
	int xd = 0, yd = 0;
	if (board_atxy(b, x + 1, y) == S_OFFBOARD || board_atxy(b, x - 1, y) == S_OFFBOARD)
		yd = 1;
	else
		xd = 1;	
	/* Direction from the border; -1 is above/left, 1 is below/right. */
	int dd = (board_atxy(b, x + yd, y + xd) == S_OFFBOARD) ? 1 : -1;
	if (DEBUGL(6))  fprintf(stderr, "xd %d yd %d dd %d\n", xd, yd, dd);
	
	/* | ? ?
	 * | . O #
	 * | c X #
	 * | . O #
	 * | ? ?   */
	/* This is normally caught, unless we have friends both above
	 * and below... */
	if (board_atxy(b, x + xd * 2, y + yd * 2) == lcolor &&
	    board_atxy(b, x - xd * 2, y - yd * 2) == lcolor)
		return false;

	/* ...or can't block where we need because of shortage
	 * of liberties. */
	group_t g1 = group_atxy(b, x + xd - yd * dd, y + yd - xd * dd);
	int libs1 = board_group_info(b, g1).libs;
	group_t g2 = group_atxy(b, x - xd - yd * dd, y - yd - xd * dd);
	int libs2 = board_group_info(b, g2).libs;
	if (DEBUGL(6))  fprintf(stderr, "libs1 %d libs2 %d\n", libs1, libs2);	
	/* Already in atari? */
	if (libs1 < 2 || libs2 < 2)  return false;
	/* Would be self-atari? */
	if (libs1 < 3 && (board_atxy(b, x + xd * 2, y + yd * 2) != S_NONE
			  || coord_is_adjecent(board_group_info(b, g1).lib[0], board_group_info(b, g1).lib[1])))
		return false;
	if (libs2 < 3 && (board_atxy(b, x - xd * 2, y - yd * 2) != S_NONE
			  || coord_is_adjecent(board_group_info(b, g2).lib[0], board_group_info(b, g2).lib[1])))
		return false;
	return true;
}


static int middle_ladder_walk(board_t *b, group_t laddered, enum stone lcolor, coord_t prevmove, int len);

static int
middle_ladder_chase(board_t *b, group_t laddered, enum stone lcolor, coord_t prevmove, int len)
{
	laddered = group_at(b, laddered);
	
	if (DEBUGL(8)) {
		board_print(b, stderr);
		fprintf(stderr, "%s c %d\n", coord2sstr(laddered), board_group_info(b, laddered).libs);
	}

	if (!laddered || board_group_info(b, laddered).libs == 1) {
		if (DEBUGL(6))  fprintf(stderr, "* we can capture now\n");
		return len;
	}
	if (board_group_info(b, laddered).libs > 2) {
		if (DEBUGL(6))  fprintf(stderr, "* we are free now\n");
		return 0;
	}

	/* Now, consider alternatives. */
	int liblist[2], libs = 0;
	for (int i = 0; i < 2; i++) {
		coord_t ataristone = board_group_info(b, laddered).lib[i];
		coord_t escape = board_group_info(b, laddered).lib[1 - i];
		if (immediate_liberty_count(b, escape) > 2 + coord_is_adjecent(ataristone, escape)) {
			/* Too much free space, ignore. */
			continue;
		}
		liblist[libs++] = i;
	}

	/* Try more promising one first */
	if (libs == 2 && immediate_liberty_count(b, board_group_info(b, laddered).lib[0]) <
	                 immediate_liberty_count(b, board_group_info(b, laddered).lib[1])) {
		liblist[0] = 1; liblist[1] = 0;
	}

	/* Try out the alternatives. */
	for (int i = 0; i < libs; i++) {		
		coord_t ataristone = board_group_info(b, laddered).lib[liblist[i]];

		with_move(b, ataristone, stone_other(lcolor), {
			/* No suicides, please. */
			if (!group_at(b, ataristone))
					break;

			if (DEBUGL(6))  fprintf(stderr, "(%d=0) ladder atari %s (%d libs)\n", i, coord2sstr(ataristone), board_group_info(b, group_at(b, ataristone)).libs);

			int l = middle_ladder_walk(b, laddered, lcolor, prevmove, len);
			if (l)  with_move_return(l);
		});
	}
	
	return 0;
}

/* Can we escape by capturing chaser ? */
static bool
chaser_capture_escapes(board_t *b, group_t laddered, enum stone lcolor, move_queue_t *ccq)
{
	for (unsigned int i = 0; i < ccq->moves; i++) {
		coord_t lib = ccq->move[i];
		if (!board_is_valid_play(b, lcolor, lib))
			continue;

#ifndef MIDDLE_LADDER_CHECK_COUNTERCAP
		return true;
#endif

		/* We can capture one of the ladder stones, investigate ... */
		if (DEBUGL(6)) {
			fprintf(stderr, "------------- can capture chaser, investigating %s -------------\n", coord2sstr(lib));
			board_print(b, stderr);
		}

		with_move_strict(b, lib, lcolor, {
			if (!middle_ladder_chase(b, laddered, lcolor, lib, 0))
				with_move_return(true); /* escape ! */		
		});

		if (DEBUGL(6))  fprintf(stderr, "-------------------------- done %s ------------------------------\n", coord2sstr(lib));
	}
	
	return false;
}


/* This is a rather expensive ladder reader. It can read out any sequences
 * where laddered group should be kept at two liberties. The recursion
 * always makes a "to-be-laddered" move and then considers the chaser's
 * two alternatives (usually, one of them is trivially refutable). The
 * function returns true if there is a branch that ends up with laddered
 * group captured, false if not (i.e. for each branch, laddered group can
 * gain three liberties). */
static int
middle_ladder_walk(board_t *b, group_t laddered, enum stone lcolor, coord_t prevmove, int len)
{
	assert(board_group_info(b, laddered).libs == 1);

	/* Check ko */
	if (b->ko.coord != pass)
		foreach_neighbor(b, last_move(b).coord, {
				if (group_at(b, c) == laddered) {
					if (DEBUGL(6))  fprintf(stderr, "* ko, no ladder\n");
					return 0;
				}
		});

	/* Check countercaptures */
	move_queue_t ccq;
	can_countercapture(b, laddered, &ccq);
	
	if (chaser_capture_escapes(b, laddered, lcolor, &ccq))
		return 0;

	/* Escape then */
	coord_t nextmove = board_group_info(b, laddered).lib[0];
	if (DEBUGL(6))  fprintf(stderr, "  ladder escape %s\n", coord2sstr(nextmove));
	with_move_strict(b, nextmove, lcolor, {
		len = middle_ladder_chase(b, laddered, lcolor, nextmove, len + 1);
	});

	return len;
}

static __thread int length = 0;

bool
is_middle_ladder(board_t *b, group_t laddered)
{
	coord_t coord = board_group_info(b, laddered).lib[0];
	enum stone lcolor = board_at(b, group_base(laddered));

	/* If we can move into empty space or do not have enough space
	 * to escape, this is obviously not a ladder. */
	if (immediate_liberty_count(b, coord) != 2) {
		if (DEBUGL(5))  fprintf(stderr, "no ladder, wrong free space\n");
		return false;
	}

	/* A fair chance for a ladder. Group in atari, with some but limited
	 * space to escape. Time for the expensive stuff - play it out and
	 * start selective 2-liberty search. */
	length = middle_ladder_walk(b, laddered, lcolor, pass, 0);

	if (DEBUGL(6) && length)  fprintf(stderr, "is_ladder(): stones: %i  length: %i\n",
					  group_stone_count(b, laddered, 50), length);

	return (length != 0);
}

bool
is_middle_ladder_any(board_t *b, group_t laddered)
{
	enum stone lcolor = board_at(b, group_base(laddered));
	
	length = middle_ladder_walk(b, laddered, lcolor, pass, 0);
	return (length != 0);
}

bool
wouldbe_ladder(board_t *b, group_t group, coord_t chaselib)
{
	assert(board_group_info(b, group).libs == 2);
	
	enum stone lcolor = board_at(b, group_base(group));
	enum stone other_color = stone_other(lcolor);
	coord_t escapelib = board_group_other_lib(b, group, chaselib);

	if (DEBUGL(6))  fprintf(stderr, "would-be ladder check - does %s %s play out chasing move %s?\n",
				stone2str(lcolor), coord2sstr(escapelib), coord2sstr(chaselib));

	if (immediate_liberty_count(b, escapelib) != 2) {
		if (DEBUGL(5))  fprintf(stderr, "no ladder, or overly trivial for a ladder\n");
		return false;
	}

	// FIXME should assert instead here
	// See ~/src/pachi_bugs/silly_misread3 for example that breaks it
	if (!board_is_valid_play(b, other_color, chaselib) ||
	    is_selfatari(b, other_color, chaselib) )   // !can_play_on_lib() sortof       
		return false;

	bool ladder = false;
	with_move(b, chaselib, other_color, {
		ladder = is_ladder_any(b, group, true);
	});
	
	return ladder;
}


bool
wouldbe_ladder_any(board_t *b, group_t group, coord_t chaselib)
{
	assert(board_group_info(b, group).libs == 2);
	
	enum stone lcolor = board_at(b, group_base(group));
	enum stone other_color = stone_other(lcolor);

	// FIXME should assert instead here
	if (!board_is_valid_play_no_suicide(b, other_color, chaselib))
		return false;

	bool ladder = false;
	with_move(b, chaselib, other_color, {
		ladder = is_ladder_any(b, group, true);
	});

	return ladder;
}

/* Laddered group can't escape, but playing it out could still be useful.
 *
 *      . . . * . . .    For example, life & death:
 *      X O O X O O X
 *      X X O O O X X
 *          X X X     
 *
 * Try to weed out as many useless moves as possible while still allowing these ...
 * Call right after is_ladder() succeeded, uses static state.  
 *
 * XXX can also be useful in other situations ? Should be pretty rare hopefully */
bool
useful_ladder(board_t *b, group_t laddered)
{
	if (length >= 4 ||
	    group_stone_count(b, laddered, 6) > 5 ||
	    neighbor_is_safe(b, laddered))
		return false;

	coord_t lib = board_group_info(b, laddered).lib[0];
	enum stone lcolor = board_at(b, laddered);

	/* Check capturing group is surrounded */
	with_move(b, lib, stone_other(lcolor), {	
		assert(!group_at(b, laddered));
		if (!dragon_is_surrounded(b, lib))
			with_move_return(false);
	});
	
	/* Group safe even after escaping + capturing us ? */
	// XXX can need to walk ladder twice to become useful ...
	bool still_safe = false, cap_ok = false;
	with_move(b, lib, lcolor, {
		if (!group_at(b, lib))  break;
		
		group_t g = group_at(b, lib);
		// Try diff move order, could be suicide !
		for (int i = 0; !cap_ok && i < board_group_info(b, g).libs; i++) {
			coord_t cap = board_group_info(b, g).lib[i];
			with_move(b, cap, stone_other(lcolor), {				       
				if (!group_at(b, lib) || !group_at(b, cap))  break;
				
				coord_t cap = board_group_info(b, group_at(b, lib)).lib[0];						
				with_move(b, cap, stone_other(lcolor), {
						assert(!group_at(b, lib));
						cap_ok = true;
						still_safe = dragon_is_safe(b, group_at(b, cap), stone_other(lcolor));
				});
			});
		}
	});
	if (still_safe)  return false;

	/* Does it look useful as selfatari ? */
	foreach_neighbor(b, lib, {
		if (board_at(b, c) != S_NONE)  continue;
		
		with_move(b, c, stone_other(lcolor), {
			if (board_group_info(b, group_at(b, c)).libs - 1 <= 1)
				break;
			if (!is_bad_selfatari(b, lcolor, lib))
				with_move_return(true);
		});
	});
	return false;
}

static bool
is_double_atari(board_t *b, coord_t c, enum stone color)
{
	if (board_at(b, c) != S_NONE ||
	    immediate_liberty_count(b, c) < 2 ||  /* can't play there (hack) */
	    neighbor_count_at(b, c, stone_other(color)) != 2)
		return false;

	int ataris = 0;
	foreach_neighbor(b, c, {
		if (board_at(b, c) == stone_other(color) &&
		    board_group_info(b, group_at(b, c)).libs == 2)
			ataris++;
	});
	
	return (ataris >= 2);
}

static bool
ladder_with_tons_of_double_ataris(board_t *b, group_t laddered, enum stone color)
{
	assert(board_at(b, laddered) == stone_other(color));

	int double_ataris = 0;
	foreach_in_group(b, laddered) {
		coord_t stone = c;
		foreach_diag_neighbor(b, stone) {
			if (is_double_atari(b, c, stone_other(color)))
				double_ataris++;
		} foreach_diag_neighbor_end;
	} foreach_in_group_end;

	return (double_ataris >= 2);
}

bool
harmful_ladder_atari(board_t *b, coord_t atari, enum stone color)
{
	assert(board_at(b, atari) == S_NONE);
	
	if (neighbor_count_at(b, atari, stone_other(color)) != 1)
		return false;

	foreach_neighbor(b, atari, {
		if (board_at(b, c) != stone_other(color))  continue;
		
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs != 2)      continue;
		
		if (ladder_with_tons_of_double_ataris(b, g, color) &&              // getting ugly ...
		    !wouldbe_ladder_any(b, g, atari))                              // and non-working ladder
			return true;
	});

	return false;
}

