#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG
#include "board.h"
#include "debug.h"

#if 0
#define profiling_noinline __attribute__((noinline))
#else
#define profiling_noinline
#endif

#define gi_granularity 4
#define gi_allocsize(gids) ((1 << gi_granularity) + ((gids) >> gi_granularity) * (1 << gi_granularity))


int
board_quick_cmp(struct board *b1, struct board *b2)
{
	if (b1->size != b2->size ||
	    b1->size2 != b2->size2 ||
	    b1->bits2 != b2->bits2 ||
	    b1->captures[S_BLACK] != b2->captures[S_BLACK] ||
	    b1->captures[S_WHITE] != b2->captures[S_WHITE] ||
	    b1->moves != b2->moves) {
		fprintf(stderr, "differs in main vars\n");
		return 1;
	}
	if (move_cmp(&b1->last_move, &b2->last_move) ||
	    move_cmp(&b1->last_move2, &b2->last_move2)) {
		fprintf(stderr, "differs in last_move\n");
		return 1;
	}
	if (move_cmp(&b1->ko, &b2->ko) ||
	    move_cmp(&b1->last_ko, &b2->last_ko) ||
	    b1->last_ko_age != b2->last_ko_age) {
		fprintf(stderr, "differs in ko\n");
		return 1;
	}

	int bsize = board_size2(b1) * sizeof(*b1->b);
	int gsize = board_size2(b1) * sizeof(*b1->g);
	//int fsize = board_size2(b1) * sizeof(*b1->f);
 	int nsize = board_size2(b1) * sizeof(*b1->n);
	int psize = board_size2(b1) * sizeof(*b1->p);
	//int hsize = board_size2(b1) * 2 * sizeof(*b1->h);
	int gisize = board_size2(b1) * sizeof(*b1->gi);
	//int csize = board_size2(board) * sizeof(*b1->c);
	//int ssize = board_size2(board) * sizeof(*b1->spathash);
	//int p3size = board_size2(board) * sizeof(*b1->pat3);
	//int tsize = board_size2(board) * sizeof(*b1->t);
	//int tqsize = board_size2(board) * sizeof(*b1->t);

	//int cdsize = board_size2(b1) * sizeof(*b1->coord);

	if (memcmp(b1->b,  b2->b,  bsize)) {
		fprintf(stderr, "differs in b\n");  return 1;  }
	if (memcmp(b1->g,  b2->g,  gsize)) {
		fprintf(stderr, "differs in g\n");  return 1;  }
	if (memcmp(b1->n,  b2->n,  nsize)) {
		fprintf(stderr, "differs in n\n");  return 1;  }
	if (memcmp(b1->p,  b2->p,  psize)) {
		fprintf(stderr, "differs in p\n");  return 1;  }
	if (memcmp(b1->gi, b2->gi, gisize)) {
		fprintf(stderr, "differs in gi\n");  return 1;  }

	return 0;
}


static void
board_group_find_extra_libs(struct board *board, group_t group, struct group *gi, coord_t avoid)
{
	/* Add extra liberty from the board to our liberty list. */
	unsigned char watermark[board_size2(board) / 8];
	memset(watermark, 0, sizeof(watermark));
#define watermark_get(c)	(watermark[c >> 3] & (1 << (c & 7)))
#define watermark_set(c)	watermark[c >> 3] |= (1 << (c & 7))

	for (int i = 0; i < GROUP_KEEP_LIBS - 1; i++)
		watermark_set(gi->lib[i]);
	watermark_set(avoid);

	foreach_in_group(board, group) {
		coord_t coord2 = c;
		foreach_neighbor(board, coord2, {
			if (board_at(board, c) + watermark_get(c) != S_NONE)
				continue;
			watermark_set(c);
			gi->lib[gi->libs++] = c;
			if (unlikely(gi->libs >= GROUP_KEEP_LIBS))
				return;
		} );
	} foreach_in_group_end;
#undef watermark_get
#undef watermark_set
}

static void
check_libs_consistency(struct board *board, group_t g)
{ }

static void
board_group_addlib(struct board *board, group_t group, coord_t coord)
{
	if (DEBUGL(7)) {
		fprintf(stderr, "Group %d[%s] %d: Adding liberty %s\n",
			group_base(group), coord2sstr(group_base(group), board),
			board_group_info(board, group).libs, coord2sstr(coord, board));
	}

	check_libs_consistency(board, group);

	struct group *gi = &board_group_info(board, group);
	//bool onestone = group_is_onestone(board, group);
	if (gi->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0
			/* Seems extra branch just slows it down */
			if (!gi->lib[i])
				break;
#endif
			if (unlikely(gi->lib[i] == coord))
				return;
		}
		gi->lib[gi->libs++] = coord;
	}

	check_libs_consistency(board, group);
}


static void
board_group_rmlib(struct board *board, group_t group, coord_t coord)
{
	if (DEBUGL(7)) {
		fprintf(stderr, "Group %d[%s] %d: Removing liberty %s\n",
			group_base(group), coord2sstr(group_base(group), board),
			board_group_info(board, group).libs, coord2sstr(coord, board));
	}

	struct group *gi = &board_group_info(board, group);
	//bool onestone = group_is_onestone(board, group);
	for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0
		/* Seems extra branch just slows it down */
		if (!gi->lib[i])
			break;
#endif
		if (likely(gi->lib[i] != coord))
			continue;

		//coord_t lib = 
		gi->lib[i] = gi->lib[--gi->libs];
		gi->lib[gi->libs] = 0;

		check_libs_consistency(board, group);

		/* Postpone refilling lib[] until we need to. */
		assert(GROUP_REFILL_LIBS > 1);
		if (gi->libs > GROUP_REFILL_LIBS)
			return;
		if (gi->libs == GROUP_REFILL_LIBS)
			board_group_find_extra_libs(board, group, gi, coord);

		return;
	}

	/* This is ok even if gi->libs < GROUP_KEEP_LIBS since we
	 * can call this multiple times per coord. */
	check_libs_consistency(board, group);
	return;
}


/* This is a low-level routine that doesn't maintain consistency
 * of all the board data structures. */
static void
board_remove_stone(struct board *board, group_t group, coord_t c)
{
	enum stone color = board_at(board, c);
	board_at(board, c) = S_NONE;
	group_at(board, c) = 0;

	/* Increase liberties of surrounding groups */
	coord_t coord = c;
	foreach_neighbor(board, coord, {
		dec_neighbor_count_at(board, c, color);
		group_t g = group_at(board, c);
		if (g && g != group)
			board_group_addlib(board, g, coord);
	});
}

static int profiling_noinline
board_group_capture(struct board *board, group_t group)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_remove_stone(board, group, c);
		stones++;
	} foreach_in_group_end;

	struct group *gi = &board_group_info(board, group);
	assert(gi->libs == 0);
	memset(gi, 0, sizeof(*gi));

	return stones;
}

static void profiling_noinline
add_to_group(struct board *board, group_t group, coord_t prevstone, coord_t coord)
{
	group_at(board, coord) = group;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord;

	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			board_group_addlib(board, group, c);
	});

	if (DEBUGL(8))
		fprintf(stderr, "add_to_group: added (%d,%d ->) %d,%d (-> %d,%d) to group %d\n",
			coord_x(prevstone, board), coord_y(prevstone, board),
			coord_x(coord, board), coord_y(coord, board),
			groupnext_at(board, coord) % board_size(board), groupnext_at(board, coord) / board_size(board),
			group_base(group));
}

static void profiling_noinline
merge_groups(struct board *board, group_t group_to, group_t group_from, struct board_undo *u)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merging groups %d -> %d\n",
			group_base(group_from), group_base(group_to));
	struct group *gi_from = &board_group_info(board, group_from);
	struct group *gi_to = &board_group_info(board, group_to);
	// bool onestone_from = group_is_onestone(board, group_from);
	// bool onestone_to = group_is_onestone(board, group_to);

	if (DEBUGL(7))
		fprintf(stderr,"---- (froml %d, tol %d)\n", gi_from->libs, gi_to->libs);

	if (gi_to->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < gi_from->libs; i++) {
			for (int j = 0; j < gi_to->libs; j++)
				if (gi_to->lib[j] == gi_from->lib[i])
					goto next_from_lib;
			gi_to->lib[gi_to->libs++] = gi_from->lib[i];
			if (gi_to->libs >= GROUP_KEEP_LIBS)
				break;
next_from_lib:;
		}
	}

	//if (gi_to->libs == 1) {
	//	coord_t lib = board_group_info(board, group_to).lib[0];
	//}

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;
	
	u->merged[++u->nmerged_tmp].last = last_in_group;
	groupnext_at(board, last_in_group) = groupnext_at(board, group_base(group_to));
	groupnext_at(board, group_base(group_to)) = group_base(group_from);
	memset(gi_from, 0, sizeof(struct group));

	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merged group: %d\n",
			group_base(group_to));
}


static group_t profiling_noinline
new_group(struct board *board, coord_t coord)
{
	group_t group = coord;
	struct group *gi = &board_group_info(board, group);
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			/* board_group_addlib is ridiculously expensive for us */
#if GROUP_KEEP_LIBS < 4
			if (gi->libs < GROUP_KEEP_LIBS)
#endif
			gi->lib[gi->libs++] = c;
	});

	group_at(board, coord) = group;
	groupnext_at(board, coord) = 0;

	check_libs_consistency(board, group);

	if (DEBUGL(8))
		fprintf(stderr, "new_group: added %d,%d to group %d\n",
			coord_x(coord, board), coord_y(coord, board),
			group_base(group));

	return group;
}

static inline void
undo_save_merge(struct board *b, struct board_undo *u, group_t g, coord_t c)
{
	if (g == u->merged[0].group || g == u->merged[1].group || 
	    g == u->merged[2].group || g == u->merged[3].group)
		return;
	
	int i = u->nmerged++;
	if (!i)
		u->inserted = c;
	u->merged[i].group = g;
	u->merged[i].last = 0;   // can remove
	u->merged[i].info = board_group_info(b, g);
}

static inline void
undo_save_enemy(struct board *b, struct board_undo *u, group_t g)
{
	if (g == u->enemies[0].group || g == u->enemies[1].group ||
	    g == u->enemies[2].group || g == u->enemies[3].group)
		return;
	
	int i = u->nenemies++;
	u->enemies[i].group = g;
	u->enemies[i].info = board_group_info(b, g);
		
	int j = 0;
	coord_t *stones = u->enemies[i].stones;
	if (board_group_info(b, g).libs <= 1) { // Will be captured
		foreach_in_group(b, g) {
			stones[j++] = c;
		} foreach_in_group_end;
		u->captures += j;
	}
	stones[j] = 0;
}

static void
undo_save_group_info(struct board *b, coord_t coord, enum stone color, struct board_undo *u)
{
	u->next_at = groupnext_at(b, coord);

	foreach_neighbor(b, coord, {			
		group_t g = group_at(b, c);
	
		if (board_at(b, c) == color)
			undo_save_merge(b, u, g, c);
		else if (board_at(b, c) == stone_other(color)) 
			undo_save_enemy(b, u, g);
	});
}		

static void
undo_save_suicide(struct board *b, coord_t coord, enum stone color, struct board_undo *u)
{
	foreach_neighbor(b, coord, {
		if (board_at(b, c) == color) {
			// Handle suicide as a capture ...
			undo_save_enemy(b, u, group_at(b, c));
			return;
		}
	});
	assert(0);
}

static inline group_t
play_one_neighbor(struct board *board,
		  coord_t coord, enum stone color, enum stone other_color,
		  coord_t c, group_t group, struct board_undo *u)
{
	enum stone ncolor = board_at(board, c);
	group_t ngroup = group_at(board, c);

	inc_neighbor_count_at(board, c, color);
	if (!ngroup)
		return group;
	
	board_group_rmlib(board, ngroup, coord);
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: reducing libs for group %d (%d:%d,%d)\n",
			group_base(ngroup), ncolor, color, other_color);

	if (ncolor == color && ngroup != group) {
		if (!group) {
			group = ngroup;
			add_to_group(board, group, c, coord);
		} else {
			merge_groups(board, group, ngroup, u);
		}
	} else if (ncolor == other_color) {
		if (DEBUGL(8)) {
			struct group *gi = &board_group_info(board, ngroup);
			fprintf(stderr, "testing captured group %d[%s]: ", group_base(ngroup), coord2sstr(group_base(ngroup), board));
			for (int i = 0; i < GROUP_KEEP_LIBS; i++)
				fprintf(stderr, "%s ", coord2sstr(gi->lib[i], board));
			fprintf(stderr, "\n");
		}
		if (unlikely(board_group_captured(board, ngroup)))
			board_group_capture(board, ngroup);
	}
	return group;
}


/* We played on a place with at least one liberty. We will become a member of
 * some group for sure. */
static group_t profiling_noinline
board_play_outside(struct board *board, struct move *m, struct board_undo *u)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	group_t group = 0;

	undo_save_group_info(board, coord, color, u);
	
	foreach_neighbor(board, coord, {
		group = play_one_neighbor(board, coord, color, other_color, c, group, u);		
	});

	board_at(board, coord) = color;
	if (unlikely(!group))
		group = new_group(board, coord);

	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	struct move ko = { pass, S_NONE };
	board->ko = ko;

	return group;
}


/* We played in an eye-like shape. Either we capture at least one of the eye
 * sides in the process of playing, or return -1. */
static int profiling_noinline
board_play_in_eye(struct board *board, struct move *m, struct board_undo *u)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	/* Check ko: Capture at a position of ko capture one move ago */
	if (unlikely(color == board->ko.color && coord == board->ko.coord)) {
		if (DEBUGL(5))
			fprintf(stderr, "board_check: ko at %d,%d color %d\n", coord_x(coord, board), coord_y(coord, board), color);
		return -1;
	} else if (DEBUGL(6)) {
		fprintf(stderr, "board_check: no ko at %d,%d,%d - ko is %d,%d,%d\n",
			color, coord_x(coord, board), coord_y(coord, board),
			board->ko.color, coord_x(board->ko.coord, board), coord_y(board->ko.coord, board));
	}

	struct move ko = { pass, S_NONE };

	int captured_groups = 0;

	foreach_neighbor(board, coord, {
		group_t g = group_at(board, c);
		if (DEBUGL(7))
			fprintf(stderr, "board_check: group %d has %d libs\n",
				g, board_group_info(board, g).libs);
		captured_groups += (board_group_info(board, g).libs == 1);
	});

	if (likely(captured_groups == 0)) {
		if (DEBUGL(5)) {
			if (DEBUGL(6))
				board_print(board, stderr);
			fprintf(stderr, "board_check: one-stone suicide\n");
		}

		return -1;
	}

	undo_save_group_info(board, coord, color, u);

	int ko_caps = 0;
	coord_t cap_at = pass;
	foreach_neighbor(board, coord, {
		inc_neighbor_count_at(board, c, color);
		group_t group = group_at(board, c);
		if (!group)
			continue;

		board_group_rmlib(board, group, coord);
		if (DEBUGL(7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d\n",
				group_base(group));

		if (board_group_captured(board, group)) {
			ko_caps += board_group_capture(board, group);
			cap_at = c;
		}
	});
	if (ko_caps == 1) {
		ko.color = stone_other(color);
		ko.coord = cap_at; // unique
		board->last_ko = ko;
		board->last_ko_age = board->moves;
		if (DEBUGL(5))
			fprintf(stderr, "guarding ko at %d,%s\n", ko.color, coord2sstr(ko.coord, board));
	}

	board_at(board, coord) = color;
	group_t group = new_group(board, coord);

	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	board->ko = ko;

	return !!group;
}


static int __attribute__((flatten))
board_play_f(struct board *board, struct move *m, struct board_undo *u)
{
	if (DEBUGL(7)) {
		fprintf(stderr, "board_play(%s): ---- Playing %d,%d\n", coord2sstr(m->coord, board), coord_x(m->coord, board), coord_y(m->coord, board));
	}
	if (likely(!board_is_eyelike(board, m->coord, stone_other(m->color)))) {
		/* NOT playing in an eye. Thus this move has to succeed. (This
		 * is thanks to New Zealand rules. Otherwise, multi-stone
		 * suicide might fail.) */
		group_t group = board_play_outside(board, m, u);
		if (unlikely(board_group_captured(board, group))) {
			undo_save_suicide(board, m->coord, m->color, u);
			board_group_capture(board, group);
		}
		return 0;
	} else {
		return board_play_in_eye(board, m, u);
	}
}

static void
undo_init(struct board *b, struct move *m, struct board_undo *u)
{
	// Paranoid uninitialized mem test
	// memset(u, 0xff, sizeof(*u));
	
	u->last_move2 = b->last_move2;
	u->ko = b->ko;
	u->last_ko = b->last_ko;
	u->last_ko_age = b->last_ko_age;
	u->captures = 0;
	
	u->nmerged = u->nmerged_tmp = u->nenemies = 0;
	for (int i = 0; i < 4; i++)
		u->merged[i].group = u->enemies[i].group = 0;
}


int
board_quick_play(struct board *board, struct move *m, struct board_undo *u)
{
	undo_init(board, m, u);
	
	if (unlikely(is_pass(m->coord) || is_resign(m->coord))) {
		struct move nomove = { pass, S_NONE };
		board->ko = nomove;
		board->last_move2 = board->last_move;
		board->last_move = *m;
		return 0;
	}

	if (likely(board_at(board, m->coord) == S_NONE))
		return board_play_f(board, m, u);
	
	if (DEBUGL(7))
		fprintf(stderr, "board_check: stone exists\n");
	return -1;
}


/***********************************************************************************/

static inline void
undo_merge(struct board *b, struct board_undo *u, struct move *m)
{
	coord_t coord = m->coord;
	group_t group = group_at(b, coord);
	struct undo_merge *merged = u->merged;
	
	// Others groups, in reverse order ...
	for (int i = u->nmerged - 1; i > 0; i--) {
		group_t old_group = merged[i].group;
			
		board_group_info(b, old_group) = merged[i].info;
			
		groupnext_at(b, group_base(group)) = groupnext_at(b, merged[i].last);
		groupnext_at(b, merged[i].last) = 0;

#if 0
		printf("merged_group[%i]:   (last: %s)", i, coord2sstr(merged[i].last, b));
		foreach_in_group(b, old_group) {
			printf("%s ", coord2sstr(c, b));
		} foreach_in_group_end;
		printf("\n");
#endif
			
		foreach_in_group(b, old_group) {
			group_at(b, c) = old_group;
		} foreach_in_group_end;
	}

	// Restore first group
	groupnext_at(b, u->inserted) = groupnext_at(b, coord);
	board_group_info(b, merged[0].group) = merged[0].info;

#if 0
	printf("merged_group[0]: ");
	foreach_in_group(b, merged[0].group) {
		printf("%s ", coord2sstr(c, b));
	} foreach_in_group_end;
	printf("\n");
#endif
}


static inline void
restore_enemies(struct board *b, struct board_undo *u, struct move *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);
	
	struct undo_enemy *enemy = u->enemies;
	for (int i = 0; i < u->nenemies; i++) {
		group_t old_group = enemy[i].group;
			
		board_group_info(b, old_group) = enemy[i].info;
			
		coord_t *stones = enemy[i].stones;
		for (int j = 0; stones[j]; j++) {
			board_at(b, stones[j]) = other_color;
			group_at(b, stones[j]) = old_group;
			groupnext_at(b, stones[j]) = stones[j + 1];

			foreach_neighbor(b, stones[j], {
				inc_neighbor_count_at(b, c, other_color);
			});

			// Update liberties of neighboring groups
			foreach_neighbor(b, stones[j], {
					if (board_at(b, c) != color)
						continue;
					group_t g = group_at(b, c);
					if (g == u->merged[0].group || g == u->merged[1].group || g == u->merged[2].group || g == u->merged[3].group)
						continue;
					board_group_rmlib(b, g, stones[j]);
				});
		}
	}
}

static void
board_undo_stone(struct board *b, struct board_undo *u, struct move *m)
{	
	coord_t coord = m->coord;
	enum stone color = m->color;
	/* - update groups
	 * - put captures back
	 */
	
	//printf("nmerged: %i\n", u->nmerged);
	
	// Restore merged groups
	if (u->nmerged)
		undo_merge(b, u, m);
	else			// Single stone group undo
		memset(&board_group_info(b, group_at(b, coord)), 0, sizeof(struct group));
	
	board_at(b, coord) = S_NONE;
	group_at(b, coord) = 0;
	groupnext_at(b, coord) = u->next_at;
	
	foreach_neighbor(b, coord, {
			dec_neighbor_count_at(b, c, color);
	});

	// Restore enemy groups
	if (u->nenemies) {
		b->captures[color] -= u->captures;
		restore_enemies(b, u, m);
	}
}

static inline void
restore_suicide(struct board *b, struct board_undo *u, struct move *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);
	
	struct undo_enemy *enemy = u->enemies;
	for (int i = 0; i < u->nenemies; i++) {
		group_t old_group = enemy[i].group;
			
		board_group_info(b, old_group) = enemy[i].info;
			
		coord_t *stones = enemy[i].stones;
		for (int j = 0; stones[j]; j++) {
			board_at(b, stones[j]) = other_color;
			group_at(b, stones[j]) = old_group;
			groupnext_at(b, stones[j]) = stones[j + 1];

			foreach_neighbor(b, stones[j], {
				inc_neighbor_count_at(b, c, other_color);
			});

			// Update liberties of neighboring groups
			foreach_neighbor(b, stones[j], {
					if (board_at(b, c) != color)
						continue;
					group_t g = group_at(b, c);
					if (g == u->enemies[0].group || g == u->enemies[1].group || g == u->enemies[2].group || g == u->enemies[3].group)
						continue;
					board_group_rmlib(b, g, stones[j]);
				});
		}
	}
}


static void
board_undo_suicide(struct board *b, struct board_undo *u, struct move *m)
{	
	coord_t coord = m->coord;
	enum stone other_color = stone_other(m->color);
	
	// Pretend it's capture ...
	struct move m2 = { .coord = m->coord, .color = other_color };
	b->captures[other_color] -= u->captures;
	
	restore_suicide(b, u, &m2);

	undo_merge(b, u, m);

	board_at(b, coord) = S_NONE;
	group_at(b, coord) = 0;
	groupnext_at(b, coord) = u->next_at;

	foreach_neighbor(b, coord, {
		dec_neighbor_count_at(b, c, m->color);
	});

}



void
board_quick_undo(struct board *b, struct move *m, struct board_undo *u)
{
	b->last_move = b->last_move2;
	b->last_move2 = u->last_move2;
	b->ko = u->ko;
	b->last_ko = u->last_ko;
	b->last_ko_age = u->last_ko_age;
	
	if (unlikely(is_pass(m->coord) || is_resign(m->coord))) 
		return;

	b->moves--;

	if (likely(board_at(b, m->coord) == m->color))
		board_undo_stone(b, u, m);
	else if (board_at(b, m->coord) == S_NONE)
		board_undo_suicide(b, u, m);
	else
		assert(0);	/* Anything else doesn't make sense */
}

