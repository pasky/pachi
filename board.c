#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"


#define gi_granularity 4
#define gi_allocsize(gids) ((1 << gi_granularity) + ((gids) >> gi_granularity) * (1 << gi_granularity))


struct board *
board_init(void)
{
	struct board *b = calloc(1, sizeof(struct board));
	struct move m = { pass, S_NONE };
	b->last_move = b->ko = m;
	b->gi = calloc(gi_allocsize(1), sizeof(*b->gi));
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

/* Like board_copy, but faster (arrays on stack) */
#define board_copy_on_stack(b2, b1) \
	do { \
		memcpy((b2), (b1), sizeof(struct board)); \
\
		int bsize = ((b2))->size * (b2)->size * sizeof(*(b2)->b); \
		int gsize = ((b2))->size * (b2)->size * sizeof(*(b2)->g); \
		int fsize = ((b2))->size * (b2)->size * sizeof(*(b2)->f); \
		int psize = ((b2))->size * (b2)->size * sizeof(*(b2)->p); \
		int nsize = ((b2))->size * (b2)->size * sizeof(*(b2)->n); \
		void *x = alloca(bsize + gsize + fsize + psize + nsize); \
		memcpy(x, b1->b, bsize + gsize + fsize + psize + nsize); \
		((b2))->b = x; x += bsize; \
		((b2))->g = x; x += gsize; \
		((b2))->f = x; x += fsize; \
		((b2))->p = x; x += psize; \
		((b2))->n = x; x += nsize; \
\
		int gi_a = gi_allocsize((b2)->last_gid + 1); \
		(b2)->gi = alloca(gi_a * sizeof(*(b2)->gi)); \
		memcpy((b2)->gi, (b1)->gi, gi_a * sizeof(*(b2)->gi)); \
\
		(b2)->use_alloca = true; \
	} while (0)

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
	free(board->b);

	int bsize = board->size * board->size * sizeof(*board->b);
	int gsize = board->size * board->size * sizeof(*board->g);
	int fsize = board->size * board->size * sizeof(*board->f);
	int psize = board->size * board->size * sizeof(*board->p);
	int nsize = board->size * board->size * sizeof(*board->n);
	void *x = malloc(bsize + gsize + fsize + psize + nsize);
	board->b = x; x += bsize;
	board->g = x; x += gsize;
	board->f = x; x += fsize;
	board->p = x; x += psize;
	board->n = x; x += nsize;
}

void
board_clear(struct board *board)
{
	board->captures[S_BLACK] = board->captures[S_WHITE] = 0;
	board->moves = 0;

	memset(board->b, 0, board->size * board->size * sizeof(*board->b));
	memset(board->g, 0, board->size * board->size * sizeof(*board->g));

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

	int gi_a = gi_allocsize(board->last_gid + 1);
	memset(board->gi, 0, gi_a * sizeof(*board->gi));
	board->last_gid = 0;
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


static void
add_to_group(struct board *board, int gid, coord_t prevstone, coord_t coord)
{
	foreach_neighbor(board, coord) {
		if (board_at(board, c) == S_NONE) {
			board_group_libs(board, gid)++;
		}
	} foreach_neighbor_end;
	group_at(board, coord) = gid;

	if (prevstone.pos == 0) {
		/* First stone in group */
		groupnext_at(board, coord) = 0;
		board_group(board, gid).base_stone = coord;
	} else {
		groupnext_at(board, coord) = groupnext_at(board, prevstone);
		groupnext_at(board, prevstone) = coord.pos;
	}

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
group_allocate(struct board *board)
{
	if (unlikely(gi_allocsize(board->last_gid + 1) < gi_allocsize(board->last_gid + 2))) {
		if (board->use_alloca) {
			struct group *gi;
			gi = malloc(gi_allocsize(board->last_gid + 2) * sizeof(*board->gi));
			memcpy(gi, board->gi, (board->last_gid + 1) * sizeof(*board->gi));
			board->gi = gi;
		} else {
			board->gi = realloc(board->gi, gi_allocsize(board->last_gid + 2) * sizeof(*board->gi));
		}
	}
	group_t gid = ++board->last_gid;
	memset(&board->gi[gid], 0, sizeof(*board->gi));
	return gid;
}

static int
board_play_raw(struct board *board, struct move *m, int f)
{
	struct move ko = { pass, S_NONE };
	int gid = 0;

	board->f[f] = board->f[--board->flen];
	if (unlikely(debug_level > 6))
		fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);
	board_at(board, m->coord) = m->color;

	coord_t group_stone;
	coord_pos(group_stone, 0, board);

	foreach_neighbor(board, m->coord) {
		enum stone color = board_at(board, c);
		group_t group = group_at(board, c);

		dec_neighbor_count_at(board, c, S_NONE);
		inc_neighbor_count_at(board, c, m->color);

		if (group == 0)
			continue;

		board_group_libs(board, group)--;
		if (unlikely(debug_level > 7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d: libs %d\n",
				group, board_group_libs(board, group));

		if (color == m->color && group != gid) {
			if (gid <= 0) {
				gid = group;
				group_stone = c;
			} else {
				merge_groups(board, gid, group);
			}
		} else if (color == stone_other(m->color)) {
			if (unlikely(board_group_captured(board, group))) {
				int stones = board_group_capture(board, group);
				if (stones == 1) {
					/* If we captured multiple groups at once,
					 * we can't be fighting ko so we don't need
					 * to check for that. */
					ko.color = color;
					ko.coord = c;
				}
			}
		}
	} foreach_neighbor_end;

	if (unlikely(gid <= 0))
		gid = group_allocate(board);
	add_to_group(board, gid, group_stone, m->coord);

	board->last_move = *m;
	board->moves++;
	board->ko = ko;

	return gid;
}

static int
board_play_f(struct board *board, struct move *m, int f)
{
	if (!board->prohibit_suicide
	    && !board_is_eyelike(board, &m->coord, stone_other(m->color))) {
		/* NOT nakade. Thus this move has to succeed. (This is thanks
		 * to New Zealand rules. Otherwise, multi-stone suicide might
		 * fail.) */
		int gid = board_play_raw(board, m, f);
		if (board_group_captured(board, gid)) {
			board_group_capture(board, gid);
		}
		return 0;
	}

	/* Nakade - playing inside opponent's "eye" (maybe false). Either this
	 * is a suicide, or one of the opponent's groups is going to get
	 * captured (unless the ko rule prevents that). */

	/* Check ko: Capture at a position of ko capture one move ago */
	if (unlikely(m->color == board->ko.color && coord_eq(m->coord, board->ko.coord))) {
		if (unlikely(debug_level > 5))
			fprintf(stderr, "board_check: ko at %d,%d color %d\n", coord_x(m->coord), coord_y(m->coord), m->color);
		return -1;
	}

	struct board b2;

	/* Try it! */
	board_copy_on_stack(&b2, board);
	int gid = board_play_raw(board, m, f);
	if (unlikely(debug_level > 7))
		fprintf(stderr, "board_play_raw(%d,%d,%d): %d\n", m->color, coord_x(m->coord), coord_y(m->coord), gid);

	if (unlikely(board_group_captured(board, group_at(board, m->coord)))) {
		/* oops, suicide */
		if (unlikely(debug_level > 5)) {
			if (unlikely(debug_level > 6))
				board_print(board, stderr);
			fprintf(stderr, "board_check: one-stone suicide\n");
		}
		gid = -1;
	}

	if (unlikely(gid < 0)) {
		/* Restore the original board. */
		void *b = board->b, *g = board->g, *f = board->f, *p = board->p, *n = board->n, *gi = board->gi;
		memcpy(board->b, b2.b, b2.size * b2.size * sizeof(*b2.b));
		memcpy(board->g, b2.g, b2.size * b2.size * sizeof(*b2.g));
		memcpy(board->f, b2.f, b2.size * b2.size * sizeof(*b2.f));
		memcpy(board->p, b2.p, b2.size * b2.size * sizeof(*b2.p));
		memcpy(board->n, b2.n, b2.size * b2.size * sizeof(*b2.n));
		memcpy(board->gi, b2.gi, (b2.last_gid + 1) * sizeof(*b2.gi));
		memcpy(board, &b2, sizeof(b2));
		board->b = b; board->g = g; board->f = f; board->p = p; board->n = n; board->gi = gi;
		board->use_alloca = false;
	}

	return gid;
}

int
board_play(struct board *board, struct move *m)
{
	if (unlikely(is_pass(m->coord) || is_resign(m->coord)))
		return -1;

	int f;
	for (f = 0; f < board->flen; f++)
		if (board->f[f] == m->coord.pos)
			return board_play_f(board, m, f);

	if (unlikely(debug_level > 7))
		fprintf(stderr, "board_check: stone exists\n");
	return 0;
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
	int base = random() % b->flen;
	coord_pos(*coord, base, b);

	int f;
	for (f = base; f < b->flen; f++)
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
	enum stone color_diag_libs[S_MAX];
	memset(color_diag_libs, 0, sizeof(color_diag_libs));

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
		enum stone color = board_at(board, c);
		board->captures[stone_other(color)]++;
		board_at(board, c) = S_NONE;
		group_at(board, c) = 0;
		if (unlikely(debug_level > 6))
			fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c), coord_y(c));
		board->f[board->flen++] = c.pos;
		stones++;

		/* Increase liberties of surrounding groups */
		coord_t coord = c;
		foreach_neighbor(board, coord) {
			dec_neighbor_count_at(board, c, color);
			inc_neighbor_count_at(board, c, S_NONE);
			int gid = group_at(board, c);
			if (group_at(board, c) > 0)
				board_group_libs(board, gid)++;
		} foreach_neighbor_end;
	} foreach_in_group_end;

	return stones;
}

bool
board_group_in_atari(struct board *board, int group)
{
	int libs = 0;

	bool watermark[board->size * board->size];
	memset(watermark, 0, sizeof(watermark));

	foreach_in_group(board, group) {
		coord_t coord = c;
		foreach_neighbor(board, coord) {
			if (watermark[c.pos])
				continue;
			watermark[c.pos] = true;
			if (board_at(board, c) == S_NONE)
				libs++;
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
			if (gcache[g] == GC_DUNNO)
				gcache[g] = board_group_in_atari(board, g) == 1 ? GC_DEAD : GC_ALIVE;
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
