#include <alloca.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "random.h"

int board_group_capture(struct board *board, int group);

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
	b->last_move = b->ko = m;
}

struct board *
board_init(void)
{
	struct board *b = malloc(sizeof(struct board));
	board_setup(b);
	return b;
}

struct board *
board_copy(struct board *b2, struct board *b1)
{
	memcpy(b2, b1, sizeof(struct board));

	int bsize = b2->size2 * sizeof(*b2->b);
	int gsize = b2->size2 * sizeof(*b2->g);
	int fsize = b2->size2 * sizeof(*b2->f);
	int nsize = b2->size2 * sizeof(*b2->n);
	int psize = b2->size2 * sizeof(*b2->p);
	int hsize = b2->size2 * 2 * sizeof(*b2->h);
	int gisize = b2->size2 * sizeof(*b2->gi);
	void *x = malloc(bsize + gsize + fsize + psize + nsize + hsize + gisize);
	memcpy(x, b1->b, bsize + gsize + fsize + psize + nsize + hsize + gisize);
	b2->b = x; x += bsize;
	b2->g = x; x += gsize;
	b2->f = x; x += fsize;
	b2->p = x; x += psize;
	b2->n = x; x += nsize;
	b2->h = x; x += hsize;
	b2->gi = x; x += gisize;

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
	board->size = size + 2 /* S_OFFBOARD margin */;
	board->size2 = board->size * board->size;
	if (board->b)
		free(board->b);

	int bsize = board->size2 * sizeof(*board->b);
	int gsize = board->size2 * sizeof(*board->g);
	int fsize = board->size2 * sizeof(*board->f);
	int nsize = board->size2 * sizeof(*board->n);
	int psize = board->size2 * sizeof(*board->p);
	int hsize = board->size2 * 2 * sizeof(*board->h);
	int gisize = board->size2 * sizeof(*board->gi);
	void *x = malloc(bsize + gsize + fsize + psize + nsize + hsize + gisize);
	memset(x, 0, bsize + gsize + fsize + psize + nsize + hsize + gisize);
	board->b = x; x += bsize;
	board->g = x; x += gsize;
	board->f = x; x += fsize;
	board->p = x; x += psize;
	board->n = x; x += nsize;
	board->h = x; x += hsize;
	board->gi = x; x += gisize;
}

void
board_clear(struct board *board)
{
	int size = board->size;

	board_done_noalloc(board);
	board_setup(board);
	board_resize(board, size - 2 /* S_OFFBOARD margin */);

	/* Draw the offboard margin */
	int top_row = board->size2 - board->size;
	int i;
	for (i = 0; i < board->size; i++)
		board->b[i] = board->b[top_row + i] = S_OFFBOARD;
	for (i = 0; i <= top_row; i += board->size)
		board->b[i] = board->b[board->size - 1 + i] = S_OFFBOARD;

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
	for (i = board->size; i < (board->size - 1) * board->size; i++)
		if (i % board->size != 0 && i % board->size != board->size - 1)
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
	for (x = 1; x < board->size - 1; x++)
		fprintf(f, "%c ", asdf[x - 1]);
	fprintf(f, "\n   +-");
	for (x = 1; x < board->size - 1; x++)
		fprintf(f, "--");
	fprintf(f, "+\n");
	for (y = board->size - 2; y >= 1; y--) {
		fprintf(f, "%2d | ", y);
		for (x = 1; x < board->size - 1; x++) {
			if (coord_x(board->last_move.coord, board) == x && coord_y(board->last_move.coord, board) == y)
				fprintf(f, "%c)", stone2char(board_atxy(board, x, y)));
			else
				fprintf(f, "%c ", stone2char(board_atxy(board, x, y)));
		}
		if (DEBUGL(6)) {
			fprintf(f, "| ");
			for (x = 1; x < board->size - 1; x++) {
				fprintf(f, "%d ", group_atxy(board, x, y));
			}
		}
		fprintf(f, "|\n");
	}
	fprintf(f, "   +-");
	for (x = 1; x < board->size - 1; x++)
		fprintf(f, "--");
	fprintf(f, "+\n\n");
}


/* Update board hash with given coordinate. */
static void profiling_noinline
board_hash_update(struct board *board, coord_t coord, enum stone color)
{
	board->hash ^= board->h[(color == S_BLACK ? board->size2 : 0) + coord_raw(coord)];
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_update(%d,%d,%d) ^ %llx -> %llx\n", color, coord_x(coord, board), coord_y(coord, board), board->h[color * coord_raw(coord)], board->hash);
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
			i++;
		}
		board->history_hash[i & history_hash_mask] = board->hash;
	}
}


void
board_handicap_stone(struct board *board, int x, int y, FILE *f)
{
	struct move m;
	m.color = S_BLACK;
	coord_xy(m.coord, x, y, board);

	board_play(board, &m);

	char *str = coord2str(m.coord, board);
	if (DEBUGL(1))
		fprintf(stderr, "choosing handicap %s (%d,%d)\n", str, x, y);
	fprintf(f, "%s ", str);
	free(str);
}

void
board_handicap(struct board *board, int stones, FILE *f)
{
	int margin = 3 + (board->size >= 13);
	int min = margin;
	int mid = board->size / 2;
	int max = board->size - 1 - margin;
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
	for (int i = 0; i < gi->libs; i++)
		if (board_at(board, gi->lib[i]) != S_NONE) {
			fprintf(stderr, "BOGUS LIBERTY %s of group %d[%s]\n", coord2sstr(gi->lib[i], board), g, coord2sstr(g, board));
			assert(0);
		}
#endif
}

static void
board_group_addlib(struct board *board, group_t group, coord_t coord)
{
	if (DEBUGL(7)) {
		fprintf(stderr, "Group %d[%s]: Adding liberty %s\n",
			group, coord2sstr(group, board), coord2sstr(coord, board));
	}

	check_libs_consistency(board, group);

	struct group *gi = &board_group_info(board, group);
	if (gi->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < gi->libs; i++)
			if (gi->lib[i] == coord)
				return;
		gi->lib[gi->libs++] = coord;
	}
}

static void
board_group_rmlib(struct board *board, group_t group, coord_t coord)
{
	if (DEBUGL(7)) {
		fprintf(stderr, "Group %d[%s]: Removing liberty %s\n",
			group, coord2sstr(group, board), coord2sstr(coord, board));
	}

	struct group *gi = &board_group_info(board, group);
	for (int i = 0; i < gi->libs; i++) {
		if (gi->lib[i] == coord) {
			for (i++; i < gi->libs; i++)
				gi->lib[i - 1] = gi->lib[i];
			gi->libs--;

			check_libs_consistency(board, group);
			if (gi->libs < GROUP_KEEP_LIBS - 1)
				return;
			goto find_extra_lib;
		}
	}

	/* This is ok even if gi->libs < GROUP_KEEP_LIBS since we
	 * can call this multiple times per coord. */
	check_libs_consistency(board, group);
	return;

	/* Add extra liberty from the board to our liberty list. */
find_extra_lib:;
	bool watermark[board->size2];
	memset(watermark, 0, sizeof(watermark));

	foreach_in_group(board, group) {
		coord_t coord = c;
		foreach_neighbor(board, coord, {
			if (likely(watermark[coord_raw(c)]))
				continue;
			watermark[coord_raw(c)] = true;
			if (board_at(board, c) == S_NONE) {
				bool next = false;
				for (int i = 0; i < GROUP_KEEP_LIBS - 1; i++) {
					if (gi->lib[i] == c) {
						next = true;
						break;
					}
				}
				if (!next) {
					gi->lib[gi->libs++] = c;
					return;
				}
			}
		} );
	} foreach_in_group_end;
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
		board_group_addlib(board, group_at(board, c), coord);
	});

	if (DEBUGL(6))
		fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c, board), coord_y(c, board));
	board->f[board->flen++] = coord_raw(c);
}


static void profiling_noinline
add_to_group(struct board *board, int gid, coord_t prevstone, coord_t coord)
{
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			board_group_addlib(board, gid, c);
	});

	group_at(board, coord) = gid;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord_raw(coord);

	if (DEBUGL(8))
		fprintf(stderr, "add_to_group: added (%d,%d ->) %d,%d (-> %d,%d) to group %d\n",
			coord_x(prevstone, board), coord_y(prevstone, board),
			coord_x(coord, board), coord_y(coord, board),
			groupnext_at(board, coord) % board->size, groupnext_at(board, coord) / board->size,
			gid);
}

static void profiling_noinline
merge_groups(struct board *board, group_t group_to, group_t group_from)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merging groups %d -> %d\n",
			group_from, group_to);

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;
	groupnext_at(board, last_in_group) = groupnext_at(board, group_to);
	groupnext_at(board, group_to) = group_from;

	struct group *gi_from = &board_group_info(board, group_from);
	struct group *gi_to = &board_group_info(board, group_to);
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

	memset(gi_from, 0, sizeof(struct group));

	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merged group: %d\n",
			group_to);
}

static group_t profiling_noinline
new_group(struct board *board, coord_t coord)
{
	group_t gid = coord_raw(coord);
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			board_group_addlib(board, gid, c);
	});


	group_at(board, coord) = gid;
	groupnext_at(board, coord) = 0;

	if (DEBUGL(8))
		fprintf(stderr, "new_group: added %d,%d to group %d\n",
			coord_x(coord, board), coord_y(coord, board),
			gid);

	return gid;
}

/* We played on a place with at least one liberty. We will become a member of
 * some group for sure. */
static int profiling_noinline
board_play_outside(struct board *board, struct move *m, int f)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	int gid = 0;

	board->f[f] = board->f[--board->flen];
	if (DEBUGL(6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);

	foreach_neighbor(board, coord, {
		enum stone ncolor = board_at(board, c);
		group_t ngroup = group_at(board, c);

		inc_neighbor_count_at(board, c, color);

		if (!ngroup)
			continue;

		board_group_rmlib(board, ngroup, coord);
		if (DEBUGL(7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d (%d:%d,%d)\n",
				ngroup, ncolor, color, other_color);

		if (ncolor == color && ngroup != gid) {
			if (gid <= 0) {
				gid = ngroup;
				add_to_group(board, gid, c, coord);
			} else {
				merge_groups(board, gid, ngroup);
			}
		} else if (ncolor == other_color) {
			if (DEBUGL(8)) {
				struct group *gi = &board_group_info(board, ngroup);
				fprintf(stderr, "testing captured group %d[%s]: ", ngroup, coord2sstr(ngroup, board));
				for (int i = 0; i < gi->libs; i++)
					fprintf(stderr, "%s ", coord2sstr(gi->lib[i], board));
				fprintf(stderr, "\n");
			}
			if (unlikely(board_group_captured(board, ngroup)))
				board_group_capture(board, ngroup);
		}
	});

	if (unlikely(gid <= 0))
		gid = new_group(board, coord);

	board_at(board, coord) = color;
	board->last_move = *m;
	board->moves++;
	board_hash_update(board, coord, color);
	struct move ko = { pass, S_NONE };
	board->ko = ko;

	return gid;
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

	board->f[f] = board->f[--board->flen];
	if (DEBUGL(6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);

	int captured_groups = 0;

	foreach_neighbor(board, coord, {
		group_t group = group_at(board, c);
		if (!group)
			continue;

		board_group_rmlib(board, group, coord);
		if (DEBUGL(7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d\n",
				group);

		if (unlikely(board_group_captured(board, group))) {
			captured_groups++;
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

	if (likely(captured_groups == 0)) {
		if (DEBUGL(5)) {
			if (DEBUGL(6))
				board_print(board, stderr);
			fprintf(stderr, "board_check: one-stone suicide\n");
		}

		foreach_neighbor(board, coord, {
			board_group_addlib(board, group_at(board, c), coord);
			if (DEBUGL(7))
				fprintf(stderr, "board_play_raw: restoring libs for group %d\n",
					group_at(board, c));
		});

		coord_t c = coord;
		if (DEBUGL(6))
			fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c, board), coord_y(c, board));
		board->f[board->flen++] = coord_raw(c);
		return -1;
	}

	foreach_neighbor(board, coord, {
		inc_neighbor_count_at(board, c, color);
	});

	board_at(board, coord) = color;

	board->last_move = *m;
	board->moves++;
	board_hash_update(board, coord, color);
	board_hash_commit(board);
	board->ko = ko;

	return new_group(board, coord);
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
		int gid = board_play_outside(board, m, f);
		if (unlikely(board_group_captured(board, gid))) {
			board_group_capture(board, gid);
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
	if (unlikely(is_pass(m->coord) || is_resign(m->coord)))
		return 0;

	int f;
	for (f = 0; f < board->flen; f++)
		if (board->f[f] == coord_raw(m->coord))
			return board_play_f(board, m, f);

	if (DEBUGL(7))
		fprintf(stderr, "board_check: stone exists\n");
	return -1;
}


static inline bool
board_try_random_move(struct board *b, enum stone color, coord_t *coord, int f)
{
	coord_raw(*coord) = b->f[f];
	if (is_pass(*coord))
		return random_pass;
	struct move m = { *coord, color };
	if (DEBUGL(6))
		fprintf(stderr, "trying random move %d: %d,%d\n", f, coord_x(*coord, b), coord_y(*coord, b));
	return (!board_is_one_point_eye(b, coord, color) /* bad idea to play into one, usually */
	        && board_play_f(b, &m, f) >= 0);
}

void
board_play_random(struct board *b, enum stone color, coord_t *coord)
{
	int base = fast_random(b->flen);
	coord_pos(*coord, base, b);
	if (likely(board_try_random_move(b, color, coord, base)))
		return;

	int f;
	for (f = base + 1; f < b->flen; f++)
		if (board_try_random_move(b, color, coord, f))
			return;
	for (f = 0; f < base; f++)
		if (board_try_random_move(b, color, coord, f))
			return;

	*coord = pass;
}


bool
board_is_eyelike(struct board *board, coord_t *coord, enum stone eye_color)
{
	return (neighbor_count_at(board, *coord, eye_color) + neighbor_count_at(board, *coord, S_OFFBOARD)) == 4;
}

bool
board_is_one_point_eye(struct board *board, coord_t *coord, enum stone eye_color)
{
	enum stone color_diag_libs[S_MAX] = {0, 0, 0, 0};

	if (likely(neighbor_count_at(board, *coord, eye_color) + neighbor_count_at(board, *coord, S_OFFBOARD) < 4)) {
		return false;
	}

	/* XXX: We attempt false eye detection but we will yield false
	 * positives in case of http://senseis.xmp.net/?TwoHeadedDragon :-( */

	foreach_diag_neighbor(board, *coord) {
		color_diag_libs[(enum stone) board_at(board, c)]++;
	} foreach_diag_neighbor_end;
	color_diag_libs[stone_other(eye_color)] += !!color_diag_libs[S_OFFBOARD];
	return likely(color_diag_libs[stone_other(eye_color)] < 2);
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
board_group_capture(struct board *board, int group)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_remove_stone(board, c);
		stones++;
	} foreach_in_group_end;

	memset(&board_group_info(board, group), 0, sizeof(struct group));

	return stones;
}

bool
board_group_in_atari(struct board *board, group_t group, coord_t *lastlib)
{
	if (board_group_info(board, group).libs != 1)
		return false;
	*lastlib = board_group_info(board, group).lib[0];
	return true;
}

bool
board_group_can_atari(struct board *board, group_t group, coord_t lastlib[2])
{
	if (board_group_info(board, group).libs != 2)
		return false;
	lastlib[0] = board_group_info(board, group).lib[0];
	lastlib[1] = board_group_info(board, group).lib[1];
	return true;
}


/* Chinese counting */
float
board_official_score(struct board *board)
{
	int scores[S_MAX];
	memset(scores, 0, sizeof(scores));

	enum { GC_DUNNO, GC_ALIVE, GC_DEAD } gcache[board->size * board->size + 1];
	memset(gcache, 0, sizeof(gcache));

	foreach_point(board) {
		enum stone color = board_at(board, c);
		group_t g = group_at(board, c);
		if (g > 0) {
			/* There is a complication: There can be some dead
			 * stones that could not have been removed because
			 * they are in enemy territory and we can't suicide.
			 * At least we know they are in atari. */
			if (gcache[g] == GC_DUNNO) {
				coord_t x;
				gcache[g] = board_group_in_atari(board, g, &x) == 1 ? GC_DEAD : GC_ALIVE;
			}
			if (gcache[g] == GC_ALIVE)
				scores[color]++;
			else
				scores[stone_other(color)]++;
			/* XXX: But we still miss the one empty opponent's point. */

		} else if (color == S_NONE) {
			/* TODO: Count multi-point eyes */
			color = board_get_one_point_eye(board, &c);
			scores[color]++;
		}
	} foreach_point_end;

	return board->komi + scores[S_WHITE] - scores[S_BLACK];
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
