
#include "board.h"
#include "debug.h"
#include "board_undo.h"

#if 0
#define profiling_noinline __attribute__((noinline))
#else
#define profiling_noinline
#endif


/**********************************************************************************************/
/* board_quick_play() implementation */

static void
undo_init(board_t *b, move_t *m, board_undo_t *u)
{
	// Paranoid uninitialized mem test
	// memset(u, 0xff, sizeof(*u));
	
	u->last_move2 = last_move2(b);
	u->ko = b->ko;
	u->last_ko = b->last_ko;
	u->last_ko_age = b->last_ko_age;
	u->captures_end = &u->captures[0];
	u->ncaptures = 0;
	
	u->nmerged = u->nmerged_tmp = u->nenemies = 0;
	for (int i = 0; i < 4; i++)
		u->merged[i].group = u->enemies[i].group = 0;
}


static inline void
undo_save_merge(board_t *b, board_undo_t *u, group_t g, coord_t c)
{
	if (g == u->merged[0].group || g == u->merged[1].group || 
	    g == u->merged[2].group || g == u->merged[3].group)
		return;
	
	int i = u->nmerged++;
	if (!i) u->inserted = c;
	u->merged[i].group = g;
	u->merged[i].last = 0;   // can remove
	u->merged[i].info = *group_info(b, g);
}

static inline void
undo_save_enemy(board_t *b, board_undo_t *u, group_t g)
{
	if (g == u->enemies[0].group || g == u->enemies[1].group ||
	    g == u->enemies[2].group || g == u->enemies[3].group)
		return;
	
	int i = u->nenemies++;
	u->enemies[i].group = g;
	u->enemies[i].info = *group_info(b, g);
	u->enemies[i].stones = NULL;
	
	if (group_libs(b, g) <= 1) { // Will be captured
		coord_t *stones = u->enemies[i].stones = u->captures_end;
		int j = 0;
		foreach_in_group(b, g) {
			stones[j++] = c;
		} foreach_in_group_end;
		u->ncaptures += j;
		stones[j++] = 0;
		u->captures_end = &stones[j];
	}
}

static void
undo_save_group_info(board_t *b, coord_t coord, enum stone color, board_undo_t *u)
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
undo_save_suicide(board_t *b, coord_t coord, enum stone color, board_undo_t *u)
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

static void
board_commit_move(board_t *b, move_t *m)
{
	/* Not touching last_move3 / last_move4 */
	last_move2(b) = last_move(b);
	last_move(b) = *m;
	b->moves++;
}

#define BOARD_UNDO
#include "board_play.h"

int
board_quick_play(board_t *b, move_t *m, board_undo_t *u)
{
	assert(!is_resign(m->coord));  // XXX remove
	
	undo_init(b, m, u);
	b->u = u;
	
	int r = board_play_(b, m);
#ifdef BOARD_UNDO_CHECKS
	if (r >= 0)
		b->quicked++;
#endif

	b->u = NULL;
	return r;
}


/**********************************************************************************************/
/* board_quick_undo() implementation */

static inline void
undo_merge(board_t *b, board_undo_t *u, move_t *m)
{
	coord_t coord = m->coord;
	group_t group = group_at(b, coord);
	undo_merge_t *merged = u->merged;
	
	// Others groups, in reverse order ...
	for (int i = u->nmerged - 1; i > 0; i--) {
		group_t old_group = merged[i].group;
			
		*group_info(b, old_group) = merged[i].info;
			
		groupnext_at(b, group) = groupnext_at(b, merged[i].last);
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
	*group_info(b, merged[0].group) = merged[0].info;

#if 0
	printf("merged_group[0]: ");
	foreach_in_group(b, merged[0].group) {
		printf("%s ", coord2sstr(c, b));
	} foreach_in_group_end;
	printf("\n");
#endif
}


static inline void
restore_enemies(board_t *b, board_undo_t *u, move_t *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);
	
	undo_enemy_t *enemy = u->enemies;
	for (int i = 0; i < u->nenemies; i++) {
		group_t old_group = enemy[i].group;
			
		*group_info(b, old_group) = enemy[i].info;
			
		coord_t *stones = enemy[i].stones;
		if (!stones)  continue;

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
board_undo_stone(board_t *b, board_undo_t *u, move_t *m)
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
		memset(group_info(b, group_at(b, coord)), 0, sizeof(group_info_t));
	
	board_at(b, coord) = S_NONE;
	group_at(b, coord) = 0;
	groupnext_at(b, coord) = u->next_at;
	
	foreach_neighbor(b, coord, {
			dec_neighbor_count_at(b, c, color);
	});

	// Restore enemy groups
	if (u->nenemies) {
		b->captures[color] -= u->ncaptures;
		restore_enemies(b, u, m);
	}
}

static inline void
restore_suicide(board_t *b, board_undo_t *u, move_t *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);
	
	undo_enemy_t *enemy = u->enemies;
	for (int i = 0; i < u->nenemies; i++) {
		group_t old_group = enemy[i].group;
			
		*group_info(b, old_group) = enemy[i].info;
			
		coord_t *stones = enemy[i].stones;
		if (!stones)  continue;

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
					if (g == u->enemies[0].group || g == u->enemies[1].group || 
					    g == u->enemies[2].group || g == u->enemies[3].group)
						continue;
					board_group_rmlib(b, g, stones[j]);
				});
		}
	}
}


static void
board_undo_suicide(board_t *b, board_undo_t *u, move_t *m)
{	
	coord_t coord = m->coord;
	enum stone other_color = stone_other(m->color);
	
	// Pretend it's capture ...
	move_t m2 = move(m->coord, other_color);
	b->captures[other_color] -= u->ncaptures;
	
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
board_quick_undo(board_t *b, move_t *m, board_undo_t *u)
{
#ifdef BOARD_UNDO_CHECKS
	assert(quick_board(b));
	b->quicked--;
#endif
	
	last_move(b) = last_move2(b);
	last_move2(b) = u->last_move2;
	b->ko = u->ko;
	b->last_ko = u->last_ko;
	b->last_ko_age = u->last_ko_age;
	b->moves--;
	
	if (unlikely(is_pass(m->coord))) {
		b->passes[m->color]--;
		return;
	}

	if (likely(board_at(b, m->coord) == m->color))
		board_undo_stone(b, u, m);
	else if (board_at(b, m->coord) == S_NONE)
		board_undo_suicide(b, u, m);
	else
		assert(0);	/* Anything else doesn't make sense */
}


