#include <alloca.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "mq.h"
#include "random.h"

#ifdef BOARD_SPATHASH
#include "patternsp.h"
#endif
#ifdef BOARD_PAT3
#include "pattern3.h"
#endif

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
	b->last_move = b->last_move2 = b->last_ko = b->ko = m;
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
#ifdef BOARD_SPATHASH
	int ssize = board_size2(b2) * sizeof(*b2->spathash);
#else
	int ssize = 0;
#endif
#ifdef BOARD_PAT3
	int p3size = board_size2(b2) * sizeof(*b2->pat3);
#else
	int p3size = 0;
#endif
#ifdef BOARD_TRAITS
	int tsize = board_size2(b2) * sizeof(*b2->t);
#else
	int tsize = 0;
#endif
	void *x = malloc(bsize + gsize + fsize + psize + nsize + hsize + gisize + csize + ssize + p3size + tsize);
	memcpy(x, b1->b, bsize + gsize + fsize + psize + nsize + hsize + gisize + csize + ssize + p3size + tsize);
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
#ifdef BOARD_SPATHASH
	b2->spathash = x; x += ssize;
#endif
#ifdef BOARD_PAT3
	b2->pat3 = x; x += p3size;
#endif
#ifdef BOARD_TRAITS
	b2->t = x; x += tsize;
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
#ifdef BOARD_SPATHASH
	int ssize = board_size2(board) * sizeof(*board->spathash);
#else
	int ssize = 0;
#endif
#ifdef BOARD_PAT3
	int p3size = board_size2(board) * sizeof(*board->pat3);
#else
	int p3size = 0;
#endif
#ifdef BOARD_TRAITS
	int tsize = board_size2(board) * sizeof(*board->t);
#else
	int tsize = 0;
#endif
	void *x = malloc(bsize + gsize + fsize + psize + nsize + hsize + gisize + csize + ssize + p3size + tsize);
	memset(x, 0, bsize + gsize + fsize + psize + nsize + hsize + gisize + csize + ssize + p3size + tsize);
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
#ifdef BOARD_SPATHASH
	board->spathash = x; x += ssize;
#endif
#ifdef BOARD_PAT3
	board->pat3 = x; x += p3size;
#endif
#ifdef BOARD_TRAITS
	board->t = x; x += tsize;
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

	/* Setup neighborhood iterators */
	board->nei8[0] = -size - 1; // (-1,-1)
	board->nei8[1] = 1;
	board->nei8[2] = 1;
	board->nei8[3] = size - 2; // (-1,0)
	board->nei8[4] = 2;
	board->nei8[5] = size - 2; // (-1,1)
	board->nei8[6] = 1;
	board->nei8[7] = 1;
	board->dnei[0] = -size - 1;
	board->dnei[1] = 2;
	board->dnei[2] = size*2 - 2;
	board->dnei[3] = 2;

	/* Setup initial symmetry */
	board->symmetry.d = 1;
	board->symmetry.x1 = board->symmetry.y1 = board_size(board) / 2;
	board->symmetry.x2 = board->symmetry.y2 = board_size(board) - 1;
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

#ifdef BOARD_SPATHASH
	/* Initialize spatial hashes. */
	foreach_point(board) {
		for (int d = 1; d <= BOARD_SPATHASH_MAXD; d++) {
			for (int j = ptind[d]; j < ptind[d + 1]; j++) {
				ptcoords_at(x, y, c, board, j);
				board->spathash[coord_xy(board, x, y)][d - 1][0] ^=
					pthashes[0][j][board_at(board, c)];
				board->spathash[coord_xy(board, x, y)][d - 1][1] ^=
					pthashes[0][j][stone_other(board_at(board, c))];
			}
		}
	} foreach_point_end;
#endif
#ifdef BOARD_PAT3
	/* Initialize 3x3 pattern codes. */
	foreach_point(board) {
		if (board_at(board, c) == S_NONE)
			board->pat3[c] = pattern3_hash(board, c);
	} foreach_point_end;
#endif
	/* We don't need to initialize traits, they are all zero
	 * by default. */
}


static void
board_print_top(struct board *board, FILE *f, int c)
{
	for (int i = 0; i < c; i++) {
		char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
		fprintf(f, "      ");
		for (int x = 1; x < board_size(board) - 1; x++)
			fprintf(f, "%c ", asdf[x - 1]);
		fprintf(f, " ");
	}
	fprintf(f, "\n");
	for (int i = 0; i < c; i++) {
		fprintf(f, "    +-");
		for (int x = 1; x < board_size(board) - 1; x++)
			fprintf(f, "--");
		fprintf(f, "+");
	}
	fprintf(f, "\n");
}

static void
board_print_bottom(struct board *board, FILE *f, int c)
{
	for (int i = 0; i < c; i++) {
		fprintf(f, "    +-");
		for (int x = 1; x < board_size(board) - 1; x++)
			fprintf(f, "--");
		fprintf(f, "+");
	}
	fprintf(f, "\n");
}

static void
board_print_row(struct board *board, int y, FILE *f, board_cprint cprint)
{
	fprintf(f, " %2d | ", y);
	for (int x = 1; x < board_size(board) - 1; x++) {
		if (coord_x(board->last_move.coord, board) == x && coord_y(board->last_move.coord, board) == y)
			fprintf(f, "%c)", stone2char(board_atxy(board, x, y)));
		else
			fprintf(f, "%c ", stone2char(board_atxy(board, x, y)));
	}
	fprintf(f, "|");
	if (cprint) {
		fprintf(f, " %2d | ", y);
		for (int x = 1; x < board_size(board) - 1; x++) {
			cprint(board, coord_xy(board, x, y), f);
		}
		fprintf(f, "|");
	}
	fprintf(f, "\n");
}

void
board_print_custom(struct board *board, FILE *f, board_cprint cprint)
{
	fprintf(f, "Move: % 3d  Komi: %2.1f  Handicap: %d  Captures B: %d W: %d\n",
		board->moves, board->komi, board->handicap,
		board->captures[S_BLACK], board->captures[S_WHITE]);
	board_print_top(board, f, 1 + !!cprint);
	for (int y = board_size(board) - 2; y >= 1; y--)
		board_print_row(board, y, f, cprint);
	board_print_bottom(board, f, 1 + !!cprint);
	fprintf(f, "\n");
}

static void
cprint_group(struct board *board, coord_t c, FILE *f)
{
	fprintf(f, "%d ", group_base(group_at(board, c)));
}

void
board_print(struct board *board, FILE *f)
{
	board_print_custom(board, f, DEBUGL(6) ? cprint_group : NULL);
}


/* Update board hash with given coordinate. */
static void profiling_noinline
board_hash_update(struct board *board, coord_t coord, enum stone color)
{
	board->hash ^= hash_at(board, coord, color);
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_update(%d,%d,%d) ^ %"PRIhash" -> %"PRIhash"\n", color, coord_x(coord, board), coord_y(coord, board), hash_at(board, coord, color), board->hash);

#ifdef BOARD_SPATHASH
	/* Gridcular metric is reflective, so we update all hashes
	 * of appropriate ditance in OUR circle. */
	for (int d = 1; d <= BOARD_SPATHASH_MAXD; d++) {
		for (int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, coord, board, j);
			/* We either changed from S_NONE to color
			 * or vice versa; doesn't matter. */
			board->spathash[coord_xy(board, x, y)][d - 1][0] ^=
				pthashes[0][j][color] ^ pthashes[0][j][S_NONE];
			board->spathash[coord_xy(board, x, y)][d - 1][1] ^=
				pthashes[0][j][stone_other(color)] ^ pthashes[0][j][S_NONE];
		}
	}
#endif

#if defined(BOARD_PAT3)
	/* @color is not what we need in case of capture. */
	enum stone new_color = board_at(board, coord);
	if (new_color == S_NONE)
		board->pat3[coord] = pattern3_hash(board, coord);
	foreach_8neighbor(board, coord) { // internally, the loop uses fn__i=[0..7]
		if (board_at(board, c) != S_NONE)
			continue;
		board->pat3[c] &= ~(3 << (fn__i*2));
		board->pat3[c] |= new_color << (fn__i*2);
#if 0
		if (board_at(board, c) != S_OFFBOARD && pattern3_hash(board, c) != board->pat3[c]) {
			board_print(board, stderr);
			fprintf(stderr, "%s->%s %x != %x (%d-%d:%d)\n", coord2sstr(coord, board), coord2sstr(c, board), pattern3_hash(board, c), board->pat3[c], coord, c, fn__i);
			assert(0);
		}
#endif
	} foreach_8neighbor_end;
#endif
}

/* Commit current board hash to history. */
static void profiling_noinline
board_hash_commit(struct board *board)
{
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_commit %"PRIhash"\n", board->hash);
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
board_capturable_add(struct board *board, group_t group, coord_t lib)
{
	//fprintf(stderr, "group %s cap %s\n", coord2sstr(group, board), coord2sstr(lib, boarD));
#ifdef BOARD_TRAITS
	/* Increase capturable count trait of my last lib. */
	enum stone capturing_color = stone_other(board_at(board, group));
	assert(capturing_color == S_BLACK || capturing_color == S_WHITE);
	foreach_neighbor(board, lib, {
		if (DEBUGL(8) && group_at(board, c) == group)
			fprintf(stderr, "%s[%d] %s cap bump bc of %s(%d) member %s\n", coord2sstr(lib, board), trait_at(board, lib, capturing_color).cap, stone2str(capturing_color), coord2sstr(group, board), board_group_info(board, group).libs, coord2sstr(c, board));
		trait_at(board, lib, capturing_color).cap += (group_at(board, c) == group);
	});
#endif

#ifdef WANT_BOARD_C
	/* Update the list of capturable groups. */
	assert(group);
	assert(board->clen < board_size2(board));
	board->c[board->clen++] = group;
#endif
}
static void
board_capturable_rm(struct board *board, group_t group, coord_t lib)
{
	//fprintf(stderr, "group %s nocap %s\n", coord2sstr(group, board), coord2sstr(lib, board));
#ifdef BOARD_TRAITS
	/* Decrease capturable count trait of my previously-last lib. */
	enum stone capturing_color = stone_other(board_at(board, group));
	assert(capturing_color == S_BLACK || capturing_color == S_WHITE);
	foreach_neighbor(board, lib, {
		if (DEBUGL(8) && group_at(board, c) == group)
			fprintf(stderr, "%s[%d] cap dump bc of %s(%d) member %s\n", coord2sstr(lib, board), trait_at(board, lib, capturing_color).cap, coord2sstr(group, board), board_group_info(board, group).libs, coord2sstr(c, board));
		trait_at(board, lib, capturing_color).cap -= (group_at(board, c) == group);
	});
#endif

#ifdef WANT_BOARD_C
	/* Update the list of capturable groups. */
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
			board_capturable_add(board, group, coord);
		else if (gi->libs == 1)
			board_capturable_rm(board, group, gi->lib[0]);
		gi->lib[gi->libs++] = coord;
	}

	check_libs_consistency(board, group);
}

static void
board_group_find_extra_libs(struct board *board, group_t group, struct group *gi, coord_t avoid)
{
	/* Add extra liberty from the board to our liberty list. */
	unsigned char watermark[board_size2(board) / 8];
	memset(watermark, 0, sizeof(watermark));
#define watermark_get(c)	(watermark[coord_raw(c) >> 3] & (1 << (coord_raw(c) & 7)))
#define watermark_set(c)	watermark[coord_raw(c) >> 3] |= (1 << (coord_raw(c) & 7))

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

		coord_t lib = gi->lib[i] = gi->lib[--gi->libs];
		gi->lib[gi->libs] = 0;

		check_libs_consistency(board, group);

		/* Postpone refilling lib[] until we need to. */
		assert(GROUP_REFILL_LIBS > 1);
		if (gi->libs > GROUP_REFILL_LIBS)
			return;
		if (gi->libs == GROUP_REFILL_LIBS)
			board_group_find_extra_libs(board, group, gi, coord);

		if (gi->libs == 1)
			board_capturable_add(board, group, gi->lib[0]);
		else if (gi->libs == 0)
			board_capturable_rm(board, group, lib);
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
	board_hash_update(board, c, color);
#ifdef BOARD_TRAITS
	/* We mark as cannot-capture now. If this is a ko/snapback,
	 * we will get incremented later in board_group_addlib(). */
	trait_at(board, c, S_BLACK).cap = 0;
	trait_at(board, c, S_WHITE).cap = 0;
#endif

	/* Increase liberties of surrounding groups */
	coord_t coord = c;
	foreach_neighbor(board, coord, {
		dec_neighbor_count_at(board, c, color);
		group_t g = group_at(board, c);
		if (g && g != group)
			board_group_addlib(board, g, coord);
	});

	if (DEBUGL(6))
		fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c, board), coord_y(c, board));
	board->f[board->flen++] = coord_raw(c);
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

	if (board_group_info(board, group).libs == 1)
		board_capturable_rm(board, group, board_group_info(board, group).lib[0]);
	memset(&board_group_info(board, group), 0, sizeof(struct group));

	return stones;
}


static void profiling_noinline
add_to_group(struct board *board, group_t group, coord_t prevstone, coord_t coord)
{
	group_at(board, coord) = group;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord_raw(coord);

#ifdef BOARD_TRAITS
	if (board_group_info(board, group).libs == 1) {
		/* Our group is temporarily in atari; make sure the capturable
		 * counts also correspond to the newly added stone before we
		 * start adding liberties again so bump-dump ops match. */
		enum stone capturing_color = stone_other(board_at(board, group));
		assert(capturing_color == S_BLACK || capturing_color == S_WHITE);
		coord_t lib = board_group_info(board, group).lib[0];
		if (coord_is_adjecent(lib, coord, board)) {
			if (DEBUGL(8)) fprintf(stderr, "add_to_group %s: %s[%d] bump\n", coord2sstr(group, board), coord2sstr(lib, board), trait_at(board, lib, capturing_color).cap);
			trait_at(board, lib, capturing_color).cap++;
		}
	}
#endif

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
merge_groups(struct board *board, group_t group_to, group_t group_from)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merging groups %d -> %d\n",
			group_base(group_from), group_base(group_to));
	struct group *gi_from = &board_group_info(board, group_from);
	struct group *gi_to = &board_group_info(board, group_to);

	/* We do this early before the group info is rewritten. */
	if (gi_from->libs == 1)
		board_capturable_rm(board, group_from, gi_from->lib[0]);

	if (DEBUGL(7))
		fprintf(stderr,"---- (froml %d, tol %d)\n", gi_from->libs, gi_to->libs);

	if (gi_to->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < gi_from->libs; i++) {
			for (int j = 0; j < gi_to->libs; j++)
				if (gi_to->lib[j] == gi_from->lib[i])
					goto next_from_lib;
			if (gi_to->libs == 0)
				board_capturable_add(board, group_to, gi_from->lib[i]);
			else if (gi_to->libs == 1)
				board_capturable_rm(board, group_to, gi_to->lib[0]);
			gi_to->lib[gi_to->libs++] = gi_from->lib[i];
			if (gi_to->libs >= GROUP_KEEP_LIBS)
				break;
next_from_lib:;
		}
	}

#ifdef BOARD_TRAITS
	if (board_group_info(board, group_to).libs == 1) {
		/* Our group is currently in atari; make sure we properly
		 * count in even the neighbors from the other group in the
		 * capturable counter. */
		enum stone capturing_color = stone_other(board_at(board, group_to));
		assert(capturing_color == S_BLACK || capturing_color == S_WHITE);
		coord_t lib = board_group_info(board, group_to).lib[0];
		foreach_neighbor(board, lib, {
			if (DEBUGL(8) && group_at(board, c) == group_from) fprintf(stderr, "%s[%d] cap bump\n", coord2sstr(lib, board), trait_at(board, lib, capturing_color).cap);
			trait_at(board, lib, capturing_color).cap += (group_at(board, c) == group_from);
		});
	}
#endif

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;
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

	group_at(board, coord) = group;
	groupnext_at(board, coord) = 0;

	if (gi->libs == 1)
		board_capturable_add(board, group, gi->lib[0]);
	check_libs_consistency(board, group);

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

#if defined(BOARD_TRAITS) && !defined(NDEBUG)
	/* Sanity check that cap matches reality. */
	{
		int a = 0;
		foreach_neighbor(board, coord, {
			group_t g = group_at(board, c);
			a += g && (board_at(board, c) == other_color && board_group_info(board, g).libs == 1);
		});
		assert(a == trait_at(board, coord, color).cap);
	}
#endif
	foreach_neighbor(board, coord, {
		group = play_one_neighbor(board, coord, color, other_color, c, group);
	});

	board_at(board, coord) = color;
	if (unlikely(!group))
		group = new_group(board, coord);

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
#ifdef BOARD_TRAITS
	/* We _will_ for sure capture something. */
	assert(trait_at(board, coord, color).cap > 0);
#endif

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
				board->last_ko = ko;
				board->last_ko_age = board->moves;
				if (DEBUGL(5))
					fprintf(stderr, "guarding ko at %d,%s\n", ko.color, coord2sstr(ko.coord, board));
			}
		}
	});

	board_at(board, coord) = color;
	group_t group = new_group(board, coord);

	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	board_hash_update(board, coord, color);
	board_hash_commit(board);
	board_symmetry_update(board, &board->symmetry, coord);
	board->ko = ko;

	return !!group;
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

/* Owner map: 0: undecided; 1: black; 2: white; 3: dame */

/* One flood-fill iteration; returns true if next iteration
 * is required. */
static bool
board_tromp_taylor_iter(struct board *board, int *ownermap)
{
	bool needs_update = false;
	foreach_point(board) {
		/* Ignore occupied and already-dame positions. */
		if (board_at(board, c) != S_NONE || ownermap[c] == 3)
			continue;
		/* Count neighbors. */
		int nei[4] = {0};
		foreach_neighbor(board, c, {
			nei[ownermap[c]]++;
		});
		/* If we have neighbors of both colors, or dame,
		 * we are dame too. */
		if ((nei[1] && nei[2]) || nei[3]) {
			ownermap[c] = 3;
			/* Speed up the propagation. */
			foreach_neighbor(board, c, {
				if (board_at(board, c) == S_NONE)
					ownermap[c] = 3;
			});
			needs_update = true;
			continue;
		}
		/* If we have neighbors of one color, we are owned
		 * by that color, too. */
		if (!ownermap[c] && (nei[1] || nei[2])) {
			int newowner = nei[1] ? 1 : 2;
			ownermap[c] = newowner;
			/* Speed up the propagation. */
			foreach_neighbor(board, c, {
				if (board_at(board, c) == S_NONE && !ownermap[c])
					ownermap[c] = newowner;
			});
			needs_update = true;
			continue;
		}
	} foreach_point_end;
	return needs_update;
}

/* Tromp-Taylor Counting */
float
board_official_score(struct board *board, struct move_queue *q)
{

	/* A point P, not colored C, is said to reach C, if there is a path of
	 * (vertically or horizontally) adjacent points of P's color from P to
	 * a point of color C.
	 *
	 * A player's score is the number of points of her color, plus the
	 * number of empty points that reach only her color. */

	int ownermap[board_size2(board)];
	int s[4] = {0};
	const int o[4] = {0, 1, 2, 0};
	foreach_point(board) {
		ownermap[c] = o[board_at(board, c)];
		s[board_at(board, c)]++;
	} foreach_point_end;

	if (q) {
		/* Process dead groups. */
		for (int i = 0; i < q->moves; i++) {
			foreach_in_group(board, q->move[i]) {
				enum stone color = board_at(board, c);
				ownermap[c] = o[stone_other(color)];
				s[color]--; s[stone_other(color)]++;
			} foreach_in_group_end;
		}
	}

	/* We need to special-case empty board. */
	if (!s[S_BLACK] && !s[S_WHITE])
		return board->komi + board->handicap;

	while (board_tromp_taylor_iter(board, ownermap))
		/* Flood-fill... */;

	int scores[S_MAX];
	memset(scores, 0, sizeof(scores));

	foreach_point(board) {
		assert(board_at(board, c) == S_OFFBOARD || ownermap[c] != 0);
		if (ownermap[c] == 3)
			continue;
		scores[ownermap[c]]++;
	} foreach_point_end;

	return board->komi + board->handicap + scores[S_WHITE] - scores[S_BLACK];
}
