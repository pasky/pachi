#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "random.h"


#define gi_granularity 4
#define gi_allocsize(gids) ((1 << gi_granularity) + ((gids) >> gi_granularity) * (1 << gi_granularity))


static void
board_setup(struct board *b)
{
	memset(b, 0, sizeof(*b));

	struct move m = { pass, S_NONE };
	b->last_move = b->ko = m;

	b->gi = calloc(gi_allocsize(1), sizeof(*b->gi));
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

	int bsize = b2->size * b2->size * sizeof(*b2->b);
	int gsize = b2->size * b2->size * sizeof(*b2->g);
	int fsize = b2->size * b2->size * sizeof(*b2->f);
	int nsize = b2->size * b2->size * sizeof(*b2->n);
	int psize = b2->size * b2->size * sizeof(*b2->p);
	void *x = malloc(bsize + gsize + fsize + psize + nsize);
	memcpy(x, b1->b, bsize + gsize + fsize + psize + nsize);
	b2->b = x; x += bsize;
	b2->g = x; x += gsize;
	b2->f = x; x += fsize;
	b2->p = x; x += psize;
	b2->n = x; x += nsize;

	int gi_a = gi_allocsize(b2->last_gid + 1);
	b2->gi = calloc(gi_a, sizeof(*b2->gi));
	memcpy(b2->gi, b1->gi, gi_a * sizeof(*b2->gi));

	return b2;
}

void
board_done_noalloc(struct board *board)
{
	if (board->b) free(board->b);
	if (board->gi) free(board->gi);
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
	if (board->b)
		free(board->b);

	int bsize = board->size * board->size * sizeof(*board->b);
	int gsize = board->size * board->size * sizeof(*board->g);
	int fsize = board->size * board->size * sizeof(*board->f);
	int psize = board->size * board->size * sizeof(*board->p);
	int nsize = board->size * board->size * sizeof(*board->n);
	void *x = malloc(bsize + gsize + fsize + psize + nsize);
	memset(x, 0, bsize + gsize + fsize + psize + nsize);
	board->b = x; x += bsize;
	board->g = x; x += gsize;
	board->f = x; x += fsize;
	board->p = x; x += psize;
	board->n = x; x += nsize;
}

void
board_clear(struct board *board)
{
	int size = board->size;

	board_done_noalloc(board);
	board_setup(board);
	board_resize(board, size - 2 /* S_OFFBOARD margin */);

	/* Draw the offboard margin */
	int top_row = (board->size - 1) * board->size;
	int i;
	for (i = 0; i < board->size; i++)
		board->b[i] = board->b[top_row + i] = S_OFFBOARD;
	for (i = 0; i <= top_row; i += board->size)
		board->b[i] = board->b[board->size - 1 + i] = S_OFFBOARD;

	foreach_point(board) {
		coord_t coord = c;
		if (board_at(board, coord) == S_OFFBOARD)
			continue;
		foreach_neighbor(board, c) {
			inc_neighbor_count_at(board, coord, board_at(board, c));
		} foreach_neighbor_end;
	} foreach_point_end;

	/* All positions are free! Except the margin. */
	for (i = board->size; i < (board->size - 1) * board->size; i++)
		if (i % board->size != 0 && i % board->size != board->size - 1)
			board->f[board->flen++] = i;
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
			if (coord_x(board->last_move.coord) == x && coord_y(board->last_move.coord) == y)
				fprintf(f, "%c)", stone2char(board_atxy(board, x, y)));
			else
				fprintf(f, "%c ", stone2char(board_atxy(board, x, y)));
		}
		if (unlikely(debug_level > 6)) {
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


/* This is a low-level routine that doesn't maintain consistency
 * of all the board data structures. Use board_group_capture() from
 * your code. */
static void
board_remove_stone(struct board *board, coord_t c)
{
	enum stone color = board_at(board, c);
	board_at(board, c) = S_NONE;
	group_at(board, c) = 0;

	/* Increase liberties of surrounding groups */
	coord_t coord = c;
	foreach_neighbor(board, coord) {
		dec_neighbor_count_at(board, c, color);
		inc_neighbor_count_at(board, c, S_NONE);
		board_group_libs(board, group_at(board, c))++;
	} foreach_neighbor_end;

	if (unlikely(debug_level > 6))
		fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c), coord_y(c));
	board->f[board->flen++] = c.pos;
}


static void
add_to_group(struct board *board, int gid, coord_t prevstone, coord_t coord)
{
	board_group_libs(board, gid) += neighbor_count_at(board, coord, S_NONE);

	group_at(board, coord) = gid;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord.pos;

	if (unlikely(debug_level > 8))
		fprintf(stderr, "add_to_group: added (%d,%d ->) %d,%d (-> %d,%d) to group %d - libs %d\n",
			coord_x(prevstone), coord_y(prevstone),
			coord_x(coord), coord_y(coord),
			groupnext_at(board, coord) % board->size, groupnext_at(board, coord) / board->size,
			gid, board_group_libs(board, gid));
}

static void
merge_groups(struct board *board, group_t group_to, group_t group_from)
{
	if (unlikely(debug_level > 7))
		fprintf(stderr, "board_play_raw: merging groups %d(%d) -> %d(%d)\n",
			group_from, board_group_libs(board, group_from),
			group_to, board_group_libs(board, group_to));

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;
	groupnext_at(board, last_in_group) = board_group(board, group_to).base_stone.pos;
	board_group(board, group_to).base_stone.pos = board_group(board, group_from).base_stone.pos;

	board_group_libs(board, group_to) += board_group_libs(board, group_from);

	if (unlikely(debug_level > 7))
		fprintf(stderr, "board_play_raw: merged group: %d(%d)\n",
			group_to, board_group_libs(board, group_to));
}

static group_t
new_group(struct board *board, coord_t coord)
{
	if (unlikely(gi_allocsize(board->last_gid + 1) < gi_allocsize(board->last_gid + 2)))
		board->gi = realloc(board->gi, gi_allocsize(board->last_gid + 2) * sizeof(*board->gi));
	group_t gid = ++board->last_gid;
	memset(&board->gi[gid], 0, sizeof(*board->gi));

	board_group(board, gid).base_stone = coord;
	board_group_libs(board, gid) = neighbor_count_at(board, coord, S_NONE);

	group_at(board, coord) = gid;
	groupnext_at(board, coord) = 0;

	if (unlikely(debug_level > 8))
		fprintf(stderr, "new_group: added %d,%d to group %d - libs %d\n",
			coord_x(coord), coord_y(coord),
			gid, board_group_libs(board, gid));

	return gid;
}

/* We played on a place with at least one liberty. We will become a member of
 * some group for sure. */
static int
board_play_outside(struct board *board, struct move *m, int f)
{
	enum stone other_color = stone_other(m->color);

	struct move ko = { pass, S_NONE };
	int gid = 0;

	board->f[f] = board->f[--board->flen];
	if (unlikely(debug_level > 6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);
	board_at(board, m->coord) = m->color;

	foreach_neighbor(board, m->coord) {
		enum stone color = board_at(board, c);
		group_t group = group_at(board, c);

		dec_neighbor_count_at(board, c, S_NONE);
		inc_neighbor_count_at(board, c, m->color);

		board_group_libs(board, group)--;
		if (unlikely(debug_level > 7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d: libs %d\n",
				group, board_group_libs(board, group));

		if (color == m->color && group != gid) {
			if (gid <= 0) {
				gid = group;
				add_to_group(board, gid, c, m->coord);
			} else {
				merge_groups(board, gid, group);
			}
		} else if (color == other_color) {
			if (unlikely(board_group_captured(board, group)))
				board_group_capture(board, group);
		}
	} foreach_neighbor_end;

	if (unlikely(gid <= 0))
		gid = new_group(board, m->coord);

	board->last_move = *m;
	board->moves++;
	board->ko = ko;

	return gid;
}

/* We played in an eye-like shape. Either we capture at least one of the eye
 * sides in the process of playing, or return -1. */
static int
board_play_in_eye(struct board *board, struct move *m, int f)
{
	/* Check ko: Capture at a position of ko capture one move ago */
	if (unlikely(m->color == board->ko.color && coord_eq(m->coord, board->ko.coord))) {
		if (unlikely(debug_level > 5))
			fprintf(stderr, "board_check: ko at %d,%d color %d\n", coord_x(m->coord), coord_y(m->coord), m->color);
		return -1;
	}

	struct move ko = { pass, S_NONE };

	board->f[f] = board->f[--board->flen];
	if (unlikely(debug_level > 6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);

	int captured_groups = 0;

	foreach_neighbor(board, m->coord) {
		group_t group = group_at(board, c);

		board_group_libs(board, group)--;
		if (unlikely(debug_level > 7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d: libs %d\n",
				group, board_group_libs(board, group));

		if (unlikely(board_group_captured(board, group))) {
			captured_groups++;
			if (board_group_capture(board, group) == 1) {
				/* If we captured multiple groups at once,
				 * we can't be fighting ko so we don't need
				 * to check for that. */
				ko.color = board_at(board, c);
				ko.coord = c;
			}
		}
	} foreach_neighbor_end;

	if (likely(captured_groups == 0)) {
		if (unlikely(debug_level > 5)) {
			if (unlikely(debug_level > 6))
				board_print(board, stderr);
			fprintf(stderr, "board_check: one-stone suicide\n");
		}

		foreach_neighbor(board, m->coord) {
			board_group_libs(board, group_at(board, c))++;
			if (unlikely(debug_level > 7))
				fprintf(stderr, "board_play_raw: restoring libs for group %d: libs %d\n",
					group_at(board, c), board_group_libs(board, group_at(board, c)));
		} foreach_neighbor_end;

		coord_t c = m->coord;
		if (unlikely(debug_level > 6))
			fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c), coord_y(c));
		board->f[board->flen++] = c.pos;
		return -1;
	}

	foreach_neighbor(board, m->coord) {
		dec_neighbor_count_at(board, c, S_NONE);
		inc_neighbor_count_at(board, c, m->color);
	} foreach_neighbor_end;

	board_at(board, m->coord) = m->color;

	board->last_move = *m;
	board->moves++;
	board->ko = ko;

	return new_group(board, m->coord);
}

static int
board_play_f(struct board *board, struct move *m, int f)
{
	if (likely(!board_is_eyelike(board, &m->coord, stone_other(m->color)))) {
		/* NOT playing in an eye. Thus this move has to succeed. (This
		 * is thanks to New Zealand rules. Otherwise, multi-stone
		 * suicide might fail.) */
		int gid = board_play_outside(board, m, f);
		if (unlikely(board_group_captured(board, gid))) {
			board_group_capture(board, gid);
		}
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
		if (board->f[f] == m->coord.pos)
			return board_play_f(board, m, f);

	if (unlikely(debug_level > 7))
		fprintf(stderr, "board_check: stone exists\n");
	return -1;
}


static inline bool
board_try_random_move(struct board *b, enum stone color, coord_t *coord, int f)
{
	coord->pos = b->f[f];
	struct move m = { *coord, color };
	if (unlikely(debug_level > 6))
		fprintf(stderr, "trying random move %d: %d,%d\n", f, coord_x(*coord), coord_y(*coord));
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
	} foreach_neighbor_end;
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


int
board_group_capture(struct board *board, int group)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board_remove_stone(board, c);
		board->captures[stone_other(board_at(board, c))]++;
		stones++;
	} foreach_in_group_end;

	return stones;
}

bool
board_group_in_atari(struct board *board, int group, coord_t *lastlib)
{
	/* First rule out obvious fakes. */
	if (board_group_libs(board, group) > 4)
		return false;
	coord_t base_stone = board_group(board, group).base_stone;
	if (neighbor_count_at(board, base_stone, S_NONE) > 1)
		return false;

	int libs = 0;

	bool watermark[board->size * board->size];
	memset(watermark, 0, sizeof(watermark));

	foreach_in_group(board, group) {
		coord_t coord = c;
		foreach_neighbor(board, coord) {
			if (likely(watermark[c.pos]))
				continue;
			watermark[c.pos] = true;
			if (unlikely(board_at(board, c) == S_NONE))
				libs++;
			if (unlikely(libs > 1))
				return false;
			*lastlib = c;
		} foreach_neighbor_end;
	} foreach_in_group_end;

	return libs == 1;
}


/* Chinese counting */
float
board_official_score(struct board *board)
{
	int scores[S_MAX];
	memset(scores, 0, sizeof(scores));

	enum { GC_DUNNO, GC_ALIVE, GC_DEAD } gcache[board->last_gid + 1];
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
	} foreach_point_end;

	return board->komi + scores[S_WHITE] - scores[S_BLACK];
}
