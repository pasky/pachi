#include <alloca.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "random.h"

int board_group_capture(struct board *board, group_t group);

bool random_pass = false;


#if 0
#define profiling_noinline __attribute__((noinline))
#else
#define profiling_noinline
#endif

#define gi_granularity 4
#define gi_allocsize(gids) ((1 << gi_granularity) + ((gids) >> gi_granularity) * (1 << gi_granularity))


static void
board_setup(struct board *b)
{
	memset(b, 0, sizeof(*b));

	struct move m = { pass, S_NONE };
	b->last_move = b->last_move2 = b->ko = m;
}

struct board *
board_init(void)
{
	struct board *b = malloc(sizeof(struct board));
	board_setup(b);

	// Default setup
	b->size = 9 + 2;
	board_clear(b);

	return b;
}

struct board *
board_copy(struct board *b2, struct board *b1)
{
	memcpy(b2, b1, sizeof(struct board));

	int bsize = board_size2(b2) * sizeof(*b2->b);
	int gsize = board_size2(b2) * sizeof(*b2->g);
	int fsize = board_size2(b2) * sizeof(*b2->f);
	int nsize = board_size2(b2) * sizeof(*b2->n);
	int psize = board_size2(b2) * sizeof(*b2->p);
	int hsize = board_size2(b2) * 2 * sizeof(*b2->h);
	int gisize = board_size2(b2) * sizeof(*b2->gi);
#ifdef WANT_BOARD_C
	int csize = board_size2(b2) * sizeof(*b2->c);
#else
	int csize = 0;
#endif
	void *x = malloc(bsize + gsize + fsize + psize + nsize + hsize + gisize + csize);
	memcpy(x, b1->b, bsize + gsize + fsize + psize + nsize + hsize + gisize + csize);
	b2->b = x; x += bsize;
	b2->g = x; x += gsize;
	b2->f = x; x += fsize;
	b2->p = x; x += psize;
	b2->n = x; x += nsize;
	b2->h = x; x += hsize;
	b2->gi = x; x += gisize;
#ifdef WANT_BOARD_C
	b2->c = x; x += csize;
#endif

	return b2;
}

void
board_done_noalloc(struct board *board)
{
	if (board->b) free(board->b);
}

void
board_done(struct board *board)
{
	board_done_noalloc(board);
	free(board);
}

void
board_resize(struct board *board, int size)
{
#ifdef BOARD_SIZE
	assert(board_size(board) == size + 2);
#else
	board_size(board) = size + 2 /* S_OFFBOARD margin */;
	board_size2(board) = board_size(board) * board_size(board);
#endif
	if (board->b)
		free(board->b);

	int bsize = board_size2(board) * sizeof(*board->b);
	int gsize = board_size2(board) * sizeof(*board->g);
	int fsize = board_size2(board) * sizeof(*board->f);
	int nsize = board_size2(board) * sizeof(*board->n);
	int psize = board_size2(board) * sizeof(*board->p);
	int hsize = board_size2(board) * 2 * sizeof(*board->h);
	int gisize = board_size2(board) * sizeof(*board->gi);
#ifdef WANT_BOARD_C
	int csize = board_size2(board) * sizeof(*board->c);
#else
	int csize = 0;
#endif
	void *x = malloc(bsize + gsize + fsize + psize + nsize + hsize + gisize + csize);
	memset(x, 0, bsize + gsize + fsize + psize + nsize + hsize + gisize + csize);
	board->b = x; x += bsize;
	board->g = x; x += gsize;
	board->f = x; x += fsize;
	board->p = x; x += psize;
	board->n = x; x += nsize;
	board->h = x; x += hsize;
	board->gi = x; x += gisize;
#ifdef WANT_BOARD_C
	board->c = x; x += csize;
#endif
}

void
board_clear(struct board *board)
{
	int size = board_size(board);
	float komi = board->komi;

	board_done_noalloc(board);
	board_setup(board);
	board_resize(board, size - 2 /* S_OFFBOARD margin */);

	board->komi = komi;

	/* Setup initial symmetry */
	board->symmetry.d = 1;
	board->symmetry.x1 = board->symmetry.y1 = board->size / 2;
	board->symmetry.x2 = board->symmetry.y2 = board->size - 1;
	board->symmetry.type = SYM_FULL;

	/* Draw the offboard margin */
	int top_row = board_size2(board) - board_size(board);
	int i;
	for (i = 0; i < board_size(board); i++)
		board->b[i] = board->b[top_row + i] = S_OFFBOARD;
	for (i = 0; i <= top_row; i += board_size(board))
		board->b[i] = board->b[board_size(board) - 1 + i] = S_OFFBOARD;

	foreach_point(board) {
		coord_t coord = c;
		if (board_at(board, coord) == S_OFFBOARD)
			continue;
		foreach_neighbor(board, c, {
			inc_neighbor_count_at(board, coord, board_at(board, c));
		} );
	} foreach_point_end;

	/* First, pass is always a free position. */
	board->f[board->flen++] = coord_raw(pass);
	/* All positions are free! Except the margin. */
	for (i = board_size(board); i < (board_size(board) - 1) * board_size(board); i++)
		if (i % board_size(board) != 0 && i % board_size(board) != board_size(board) - 1)
			board->f[board->flen++] = i;

	/* Initialize zobrist hashtable. */
	foreach_point(board) {
		int max = (sizeof(hash_t) << history_hash_bits);
		/* fast_random() is 16-bit only */
		board->h[coord_raw(c) * 2] = ((hash_t) fast_random(max))
				| ((hash_t) fast_random(max) << 16)
				| ((hash_t) fast_random(max) << 32)
				| ((hash_t) fast_random(max) << 48);
		if (!board->h[coord_raw(c) * 2])
			/* Would be kinda "oops". */
			board->h[coord_raw(c) * 2] = 1;
		/* And once again for white */
		board->h[coord_raw(c) * 2 + 1] = ((hash_t) fast_random(max))
				| ((hash_t) fast_random(max) << 16)
				| ((hash_t) fast_random(max) << 32)
				| ((hash_t) fast_random(max) << 48);
		if (!board->h[coord_raw(c) * 2 + 1])
			board->h[coord_raw(c) * 2 + 1] = 1;
	} foreach_point_end;
}


void
board_print(struct board *board, FILE *f)
{
	fprintf(f, "Move: % 3d  Komi: %2.1f  Captures B: %d W: %d\n     ",
		board->moves, board->komi,
		board->captures[S_BLACK], board->captures[S_WHITE]);
	int x, y;
	char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
	for (x = 1; x < board_size(board) - 1; x++)
		fprintf(f, "%c ", asdf[x - 1]);
	fprintf(f, "\n   +-");
	for (x = 1; x < board_size(board) - 1; x++)
		fprintf(f, "--");
	fprintf(f, "+\n");
	for (y = board_size(board) - 2; y >= 1; y--) {
		fprintf(f, "%2d | ", y);
		for (x = 1; x < board_size(board) - 1; x++) {
			if (coord_x(board->last_move.coord, board) == x && coord_y(board->last_move.coord, board) == y)
				fprintf(f, "%c)", stone2char(board_atxy(board, x, y)));
			else
				fprintf(f, "%c ", stone2char(board_atxy(board, x, y)));
		}
		if (DEBUGL(6)) {
			fprintf(f, "| ");
			for (x = 1; x < board_size(board) - 1; x++) {
				fprintf(f, "%d ", group_base(group_atxy(board, x, y)));
			}
		}
		fprintf(f, "|\n");
	}
	fprintf(f, "   +-");
	for (x = 1; x < board_size(board) - 1; x++)
		fprintf(f, "--");
	fprintf(f, "+\n\n");
}


/* Update board hash with given coordinate. */
static void profiling_noinline
board_hash_update(struct board *board, coord_t coord, enum stone color)
{
	board->hash ^= hash_at(board, coord, color);
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_update(%d,%d,%d) ^ %llx -> %llx\n", color, coord_x(coord, board), coord_y(coord, board), hash_at(board, coord, color), board->hash);
}

/* Commit current board hash to history. */
static void profiling_noinline
board_hash_commit(struct board *board)
{
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_commit %llx\n", board->hash);
	if (likely(board->history_hash[board->hash & history_hash_mask]) == 0) {
		board->history_hash[board->hash & history_hash_mask] = board->hash;
	} else {
		hash_t i = board->hash;
		while (board->history_hash[i & history_hash_mask]) {
			if (board->history_hash[i & history_hash_mask] == board->hash) {
				if (DEBUGL(5))
					fprintf(stderr, "SUPERKO VIOLATION noted at %d,%d\n",
						coord_x(board->last_move.coord, board), coord_y(board->last_move.coord, board));
				board->superko_violation = true;
				return;
			}
			i = history_hash_next(i);
		}
		board->history_hash[i & history_hash_mask] = board->hash;
	}
}


void
board_symmetry_update(struct board *b, struct board_symmetry *symmetry, coord_t c)
{
	if (likely(symmetry->type == SYM_NONE)) {
		/* Fully degenerated already. We do not support detection
		 * of restoring of symmetry, assuming that this is too rare
		 * a case to handle. */
		return;
	}

	int x = coord_x(c, b), y = coord_y(c, b), t = board_size(b) / 2;
	int dx = board_size(b) - 1 - x; /* for SYM_DOWN */
	if (DEBUGL(6)) {
		fprintf(stderr, "SYMMETRY [%d,%d,%d,%d|%d=%d] update for %d,%d\n",
			symmetry->x1, symmetry->y1, symmetry->x2, symmetry->y2,
			symmetry->d, symmetry->type, x, y);
	}

	switch (symmetry->type) {
		case SYM_FULL:
			if (x == t && y == t) {
				/* Tengen keeps full symmetry. */
				return;
			}
			/* New symmetry now? */
			if (x == y) {
				symmetry->type = SYM_DIAG_UP;
				symmetry->x1 = symmetry->y1 = 1;
				symmetry->x2 = symmetry->y2 = board_size(b) - 1;
				symmetry->d = 1;
			} else if (dx == y) {
				symmetry->type = SYM_DIAG_DOWN;
				symmetry->x1 = symmetry->y1 = 1;
				symmetry->x2 = symmetry->y2 = board_size(b) - 1;
				symmetry->d = 1;
			} else if (x == t) {
				symmetry->type = SYM_HORIZ;
				symmetry->y1 = 1;
				symmetry->y2 = board_size(b) - 1;
				symmetry->d = 0;
			} else if (y == t) {
				symmetry->type = SYM_VERT;
				symmetry->x1 = 1;
				symmetry->x2 = board_size(b) - 1;
				symmetry->d = 0;
			} else {
break_symmetry:
				symmetry->type = SYM_NONE;
				symmetry->x1 = symmetry->y1 = 1;
				symmetry->x2 = symmetry->y2 = board_size(b) - 1;
				symmetry->d = 0;
			}
			break;
		case SYM_DIAG_UP:
			if (x == y)
				return;
			goto break_symmetry;
		case SYM_DIAG_DOWN:
			if (dx == y)
				return;
			goto break_symmetry;
		case SYM_HORIZ:
			if (x == t)
				return;
			goto break_symmetry;
		case SYM_VERT:
			if (y == t)
				return;
			goto break_symmetry;
		case SYM_NONE:
			assert(0);
			break;
	}

	if (DEBUGL(6)) {
		fprintf(stderr, "NEW SYMMETRY [%d,%d,%d,%d|%d=%d]\n",
			symmetry->x1, symmetry->y1, symmetry->x2, symmetry->y2,
			symmetry->d, symmetry->type);
	}
	/* Whew. */
}


void
board_handicap_stone(struct board *board, int x, int y, FILE *f)
{
	struct move m;
	m.color = S_BLACK; m.coord = coord_xy(board, x, y);

	board_play(board, &m);
	/* Simulate white passing; otherwise, UCT search can get confused since
	 * tree depth parity won't match the color to move. */
	board->moves++;

	char *str = coord2str(m.coord, board);
	if (DEBUGL(1))
		fprintf(stderr, "choosing handicap %s (%d,%d)\n", str, x, y);
	fprintf(f, "%s ", str);
	free(str);
}

void
board_handicap(struct board *board, int stones, FILE *f)
{
	int margin = 3 + (board_size(board) >= 13);
	int min = margin;
	int mid = board_size(board) / 2;
	int max = board_size(board) - 1 - margin;
	const int places[][2] = {
		{ min, min }, { max, max }, { max, min }, { min, max },
		{ min, mid }, { max, mid },
		{ mid, min }, { mid, max },
		{ mid, mid },
	};

	board->handicap = stones;

	if (stones == 5 || stones == 7) {
		board_handicap_stone(board, mid, mid, f);
		stones--;
	}

	int i;
	for (i = 0; i < stones; i++)
		board_handicap_stone(board, places[i][0], places[i][1], f);
}


static void __attribute__((noinline))
check_libs_consistency(struct board *board, group_t g)
{
#ifdef DEBUG
	if (!g) return;
	struct group *gi = &board_group_info(board, g);
	for (int i = 0; i < GROUP_KEEP_LIBS; i++)
		if (gi->lib[i] && board_at(board, gi->lib[i]) != S_NONE) {
			fprintf(stderr, "BOGUS LIBERTY %s of group %d[%s]\n", coord2sstr(gi->lib[i], board), g, coord2sstr(group_base(g), board));
			assert(0);
		}
#endif
}

static void
board_capturable_add(struct board *board, group_t group)
{
#ifdef WANT_BOARD_C
	//fprintf(stderr, "add of group %d (%d)\n", group_base(group), board->clen);
	assert(group);
	assert(board->clen < board_size2(board));
	board->c[board->clen++] = group;
#endif
}
static void
board_capturable_rm(struct board *board, group_t group)
{
#ifdef WANT_BOARD_C
	//fprintf(stderr, "rm of group %d\n", group_base(group));
	for (int i = 0; i < board->clen; i++) {
		if (unlikely(board->c[i] == group)) {
			board->c[i] = board->c[--board->clen];
			return;
		}
	}
	fprintf(stderr, "rm of bad group %d\n", group_base(group));
	assert(0);
#endif
}

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
		if (gi->libs == 0)
			board_capturable_add(board, group);
		else if (gi->libs == 1)
			board_capturable_rm(board, group);
		gi->lib[gi->libs++] = coord;
	}

	check_libs_consistency(board, group);
}

static void
board_group_find_extra_libs(struct board *board, group_t group, struct group *gi, coord_t avoid)
{
	/* Add extra liberty from the board to our liberty list. */
	enum stone watermark[board_size2(board)];
	memcpy(watermark, board->b, sizeof(watermark));

	for (int i = 0; i < GROUP_KEEP_LIBS - 1; i++)
		watermark[coord_raw(gi->lib[i])] = S_OFFBOARD;
	watermark[coord_raw(avoid)] = S_OFFBOARD;

	foreach_in_group(board, group) {
		coord_t coord2 = c;
		foreach_neighbor(board, coord2, {
			if (likely(watermark[coord_raw(c)] != S_NONE))
				continue;
			watermark[coord_raw(c)] = S_OFFBOARD;
			gi->lib[gi->libs++] = c;
			if (unlikely(gi->libs >= GROUP_KEEP_LIBS))
				return;
		} );
	} foreach_in_group_end;
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
	for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0
		/* Seems extra branch just slows it down */
		if (!gi->lib[i])
			break;
#endif
		if (likely(gi->lib[i] != coord))
			continue;

		gi->lib[i] = gi->lib[--gi->libs];
		gi->lib[gi->libs] = 0;

		check_libs_consistency(board, group);

		/* Postpone refilling lib[] until we need to. */
		assert(GROUP_REFILL_LIBS > 1);
		if (gi->libs > GROUP_REFILL_LIBS)
			return;
		if (gi->libs == GROUP_REFILL_LIBS)
			board_group_find_extra_libs(board, group, gi, coord);

		if (gi->libs == 1)
			board_capturable_add(board, group);
		else if (gi->libs == 0)
			board_capturable_rm(board, group);
		return;
	}

	/* This is ok even if gi->libs < GROUP_KEEP_LIBS since we
	 * can call this multiple times per coord. */
	check_libs_consistency(board, group);
	return;
}


/* This is a low-level routine that doesn't maintain consistency
 * of all the board data structures. Use board_group_capture() from
 * your code. */
static void
board_remove_stone(struct board *board, coord_t c)
{
	enum stone color = board_at(board, c);
	board_at(board, c) = S_NONE;
	group_at(board, c) = 0;
	board_hash_update(board, c, color);

	/* Increase liberties of surrounding groups */
	coord_t coord = c;
	foreach_neighbor(board, coord, {
		dec_neighbor_count_at(board, c, color);
		group_t g = group_at(board, c);
		if (g)
			board_group_addlib(board, g, coord);
	});

	if (DEBUGL(6))
		fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c, board), coord_y(c, board));
	board->f[board->flen++] = coord_raw(c);
}


static void profiling_noinline
add_to_group(struct board *board, group_t group, coord_t prevstone, coord_t coord)
{
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			board_group_addlib(board, group, c);
	});

	group_at(board, coord) = group;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord_raw(coord);

	if (DEBUGL(8))
		fprintf(stderr, "add_to_group: added (%d,%d ->) %d,%d (-> %d,%d) to group %d\n",
			coord_x(prevstone, board), coord_y(prevstone, board),
			coord_x(coord, board), coord_y(coord, board),
			groupnext_at(board, coord) % board_size(board), groupnext_at(board, coord) / board_size(board),
			group_base(group));
}

static void profiling_noinline
merge_groups(struct board *board, group_t group_to, group_t group_from)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merging groups %d -> %d\n",
			group_base(group_from), group_base(group_to));

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;
	groupnext_at(board, last_in_group) = groupnext_at(board, group_base(group_to));
	groupnext_at(board, group_base(group_to)) = group_base(group_from);

	struct group *gi_from = &board_group_info(board, group_from);
	struct group *gi_to = &board_group_info(board, group_to);
	if (gi_to->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < gi_from->libs; i++) {
			for (int j = 0; j < gi_to->libs; j++)
				if (gi_to->lib[j] == gi_from->lib[i])
					goto next_from_lib;
			if (gi_to->libs == 0)
				board_capturable_add(board, group_to);
			else if (gi_to->libs == 1)
				board_capturable_rm(board, group_to);
			gi_to->lib[gi_to->libs++] = gi_from->lib[i];
			if (gi_to->libs >= GROUP_KEEP_LIBS)
				break;
next_from_lib:;
		}
	}

	if (gi_from->libs == 1)
		board_capturable_rm(board, group_from);
	memset(gi_from, 0, sizeof(struct group));

	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merged group: %d\n",
			group_base(group_to));
}

static group_t profiling_noinline
new_group(struct board *board, coord_t coord)
{
	group_t group = coord_raw(coord);
	struct group *gi = &board_group_info(board, group);
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			/* board_group_addlib is ridiculously expensive for us */
#if GROUP_KEEP_LIBS < 4
			if (gi->libs < GROUP_KEEP_LIBS)
#endif
			gi->lib[gi->libs++] = c;
	});
	if (gi->libs == 1)
		board_capturable_add(board, group);
	check_libs_consistency(board, group);

	group_at(board, coord) = group;
	groupnext_at(board, coord) = 0;

	if (DEBUGL(8))
		fprintf(stderr, "new_group: added %d,%d to group %d\n",
			coord_x(coord, board), coord_y(coord, board),
			group_base(group));

	return group;
}

static inline group_t
play_one_neighbor(struct board *board,
		coord_t coord, enum stone color, enum stone other_color,
		coord_t c, group_t group)
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
			merge_groups(board, group, ngroup);
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
board_play_outside(struct board *board, struct move *m, int f)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	group_t group = 0;

	board->f[f] = board->f[--board->flen];
	if (DEBUGL(6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);

	foreach_neighbor(board, coord, {
			group = play_one_neighbor(board, coord, color, other_color, c, group);
	});

	if (unlikely(!group))
		group = new_group(board, coord);

	board_at(board, coord) = color;
	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	board_hash_update(board, coord, color);
	board_symmetry_update(board, &board->symmetry, coord);
	struct move ko = { pass, S_NONE };
	board->ko = ko;

	return group;
}

/* We played in an eye-like shape. Either we capture at least one of the eye
 * sides in the process of playing, or return -1. */
static int profiling_noinline
board_play_in_eye(struct board *board, struct move *m, int f)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	/* Check ko: Capture at a position of ko capture one move ago */
	if (unlikely(color == board->ko.color && coord_eq(coord, board->ko.coord))) {
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

	board->f[f] = board->f[--board->flen];
	if (DEBUGL(6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);

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
			if (board_group_capture(board, group) == 1) {
				/* If we captured multiple groups at once,
				 * we can't be fighting ko so we don't need
				 * to check for that. */
				ko.color = stone_other(color);
				ko.coord = c;
				if (DEBUGL(5))
					fprintf(stderr, "guarding ko at %d,%d,%d\n", ko.color, coord_x(ko.coord, board), coord_y(ko.coord, board));
			}
		}
	});

	board_at(board, coord) = color;

	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	board_hash_update(board, coord, color);
	board_hash_commit(board);
	board_symmetry_update(board, &board->symmetry, coord);
	board->ko = ko;

	return !!new_group(board, coord);
}

static int __attribute__((flatten))
board_play_f(struct board *board, struct move *m, int f)
{
	if (DEBUGL(7)) {
		fprintf(stderr, "board_play(): ---- Playing %d,%d\n", coord_x(m->coord, board), coord_y(m->coord, board));
	}
	if (likely(!board_is_eyelike(board, &m->coord, stone_other(m->color)))) {
		/* NOT playing in an eye. Thus this move has to succeed. (This
		 * is thanks to New Zealand rules. Otherwise, multi-stone
		 * suicide might fail.) */
		group_t group = board_play_outside(board, m, f);
		if (unlikely(board_group_captured(board, group))) {
			board_group_capture(board, group);
		}
		board_hash_commit(board);
		return 0;
	} else {
		return board_play_in_eye(board, m, f);
	}
}

int
board_play(struct board *board, struct move *m)
{
	if (unlikely(is_pass(m->coord) || is_resign(m->coord))) {
		board->last_move2 = board->last_move;
		board->last_move = *m;
		return 0;
	}

	int f;
	for (f = 0; f < board->flen; f++)
		if (board->f[f] == coord_raw(m->coord))
			return board_play_f(board, m, f);

	if (DEBUGL(7))
		fprintf(stderr, "board_check: stone exists\n");
	return -1;
}


static inline bool
board_try_random_move(struct board *b, enum stone color, coord_t *coord, int f, ppr_permit permit, void *permit_data)
{
	coord_raw(*coord) = b->f[f];
	if (unlikely(is_pass(*coord)))
		return random_pass;
	struct move m = { *coord, color };
	if (DEBUGL(6))
		fprintf(stderr, "trying random move %d: %d,%d\n", f, coord_x(*coord, b), coord_y(*coord, b));
	return (likely(!board_is_one_point_eye(b, coord, color)) /* bad idea to play into one, usually */
		&& board_is_valid_move(b, &m)
		&& (!permit || permit(permit_data, b, &m))
	        && likely(board_play_f(b, &m, f) >= 0));
}

void
board_play_random(struct board *b, enum stone color, coord_t *coord, ppr_permit permit, void *permit_data)
{
	int base = fast_random(b->flen);
	coord_pos(*coord, base, b);
	if (likely(board_try_random_move(b, color, coord, base, permit, permit_data)))
		return;

	int f;
	for (f = base + 1; f < b->flen; f++)
		if (board_try_random_move(b, color, coord, f, permit, permit_data))
			return;
	for (f = 0; f < base; f++)
		if (board_try_random_move(b, color, coord, f, permit, permit_data))
			return;

	*coord = pass;
}


bool
board_is_false_eyelike(struct board *board, coord_t *coord, enum stone eye_color)
{
	enum stone color_diag_libs[S_MAX] = {0, 0, 0, 0};

	/* XXX: We attempt false eye detection but we will yield false
	 * positives in case of http://senseis.xmp.net/?TwoHeadedDragon :-( */

	foreach_diag_neighbor(board, *coord) {
		color_diag_libs[(enum stone) board_at(board, c)]++;
	} foreach_diag_neighbor_end;
	/* For false eye, we need two enemy stones diagonally in the
	 * middle of the board, or just one enemy stone at the edge
	 * or in the corner. */
	color_diag_libs[stone_other(eye_color)] += !!color_diag_libs[S_OFFBOARD];
	return color_diag_libs[stone_other(eye_color)] >= 2;
}

bool
board_is_one_point_eye(struct board *board, coord_t *coord, enum stone eye_color)
{
	return board_is_eyelike(board, coord, eye_color)
		&& !board_is_false_eyelike(board, coord, eye_color);
}

enum stone
board_get_one_point_eye(struct board *board, coord_t *coord)
{
	if (board_is_one_point_eye(board, coord, S_WHITE))
		return S_WHITE;
	else if (board_is_one_point_eye(board, coord, S_BLACK))
		return S_BLACK;
	else
		return S_NONE;
}


int profiling_noinline
board_group_capture(struct board *board, group_t group)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_remove_stone(board, c);
		stones++;
	} foreach_in_group_end;

	if (board_group_info(board, group).libs == 1)
		board_capturable_rm(board, group);
	memset(&board_group_info(board, group), 0, sizeof(struct group));

	return stones;
}


float
board_fast_score(struct board *board)
{
	int scores[S_MAX];
	memset(scores, 0, sizeof(scores));

	foreach_point(board) {
		enum stone color = board_at(board, c);
		if (color == S_NONE)
			color = board_get_one_point_eye(board, &c);
		scores[color]++;
		// fprintf(stderr, "%d, %d ++%d = %d\n", coord_x(c, board), coord_y(c, board), color, scores[color]);
	} foreach_point_end;

	return board->komi + board->handicap + scores[S_WHITE] - scores[S_BLACK];
}

static enum stone
board_tromp_taylor_owner(struct board *board, coord_t c)
{
	int x = coord_x(c, board), y = coord_y(c, board);
	enum stone color = S_NONE;
#define TEST_REACH(xx, yy) \
	{ \
		enum stone c2 = board_atxy(board, xx, yy); \
		if (c2 != S_NONE) { \
			if (color != S_NONE && color != c2) \
				return S_NONE; \
			color = c2; \
			break; \
		} \
	}
	for (int i = x; i > 0; i--)
		TEST_REACH(i, y);
	for (int i = x; i < board_size(board) - 1; i++)
		TEST_REACH(i, y);
	for (int i = y; i > 0; i--)
		TEST_REACH(x, i);
	for (int i = y; i < board_size(board) - 1; i++)
		TEST_REACH(x, i);
	return color;
}

/* Tromp-Taylor Counting */
float
board_official_score(struct board *board)
{

	/* A point P, not colored C, is said to reach C, if there is a path of
	 * (vertically or horizontally) adjacent points of P's color from P to
	 * a point of color C.
	 *
	 * A player's score is the number of points of her color, plus the
	 * number of empty points that reach only her color. */

	int scores[S_MAX];
	memset(scores, 0, sizeof(scores));

	foreach_point(board) {
		enum stone color = board_at(board, c);
		if (color == S_NONE)
			color = board_tromp_taylor_owner(board, c);
		scores[color]++;
	} foreach_point_end;

	return board->komi + board->handicap + scores[S_WHITE] - scores[S_BLACK];
}
