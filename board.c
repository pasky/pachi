#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"


#define gi_granularity 4
#define gi_allocsize(gids) ((1 << gi_granularity) + ((gids) >> gi_granularity) * (1 << gi_granularity))


struct board *
board_init(void)
{
	struct board *b = calloc(1, sizeof(struct board));
	struct move m = { pass, S_NONE };
	b->last_move = m;
	b->gi = calloc(gi_allocsize(1), sizeof(*b->gi));
	return b;
}

struct board *
board_copy(struct board *b2, struct board *b1)
{
	memcpy(b2, b1, sizeof(struct board));

	b2->b = calloc(b2->size * b2->size, sizeof(*b2->b));
	b2->g = calloc(b2->size * b2->size, sizeof(*b2->g));
	memcpy(b2->b, b1->b, b2->size * b2->size * sizeof(*b2->b));
	memcpy(b2->g, b1->g, b2->size * b2->size * sizeof(*b2->g));

	int gi_a = gi_allocsize(b2->last_gid + 1);
	b2->gi = calloc(gi_a, sizeof(*b2->gi));
	memcpy(b2->gi, b1->gi, gi_a * sizeof(*b2->gi));

	return b2;
}

/* Like board_copy, but faster (arrays on stack) and with only read-only
 * gid cache */
#define board_copy_on_stack(b2, b1) \
	do { \
		memcpy((b2), (b1), sizeof(struct board)); \
		(b2)->b = alloca((b2)->size * (b2)->size * sizeof(*(b2)->b)); \
		(b2)->g = alloca((b2)->size * (b2)->size * sizeof(*(b2)->g)); \
		memcpy((b2)->b, (b1)->b, (b2)->size * (b2)->size * sizeof(*(b2)->b)); \
		memcpy((b2)->g, (b1)->g, (b2)->size * (b2)->size * sizeof(*(b2)->g)); \
		int gi_a = gi_allocsize((b2)->last_gid + 1); \
		(b2)->gi = alloca(gi_a * sizeof(*(b2)->gi)); \
		memcpy((b2)->gi, (b1)->gi, gi_a * sizeof(*(b2)->gi)); \
	} while (0)

void
board_done_noalloc(struct board *board)
{
	if (board->b) free(board->b);
	if (board->g) free(board->g);
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
	board->size = size;
	board->b = realloc(board->b, board->size * board->size * sizeof(*board->b));
	board->g = realloc(board->g, board->size * board->size * sizeof(*board->g));
}

void
board_clear(struct board *board)
{
	board->captures[S_BLACK] = board->captures[S_WHITE] = 0;
	board->moves = 0;

	memset(board->b, 0, board->size * board->size * sizeof(*board->b));
	memset(board->g, 0, board->size * board->size * sizeof(*board->g));

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
	for (x = 0; x < board->size; x++)
		fprintf(f, "%c ", asdf[x]);
	fprintf(f, "\n   +-");
	for (x = 0; x < board->size; x++)
		fprintf(f, "--");
	fprintf(f, "+\n");
	for (y = board->size - 1; y >= 0; y--) {
		fprintf(f, "%2d | ", y + 1);
		for (x = 0; x < board->size; x++) {
			if (board->last_move.coord.x == x && board->last_move.coord.y == y)
				fprintf(f, "%c)", stone2char(board_atxy(board, x, y)));
			else
				fprintf(f, "%c ", stone2char(board_atxy(board, x, y)));
		}
		fprintf(f, "|\n");
	}
	fprintf(f, "   +-");
	for (x = 0; x < board->size; x++)
		fprintf(f, "--");
	fprintf(f, "+\n\n");
}


static void
group_add(struct board *board, int gid, struct coord c)
{
	group_at(board, c) = gid;
}


int
board_play_raw(struct board *board, struct move *m, bool check_valid)
{
	int gid = 0;

	if (check_valid && !board_valid_move(board, m, false))
		return 0;

	if (is_pass(m->coord) || is_resign(m->coord))
		goto record;

	board_at(board, m->coord) = m->color;

	foreach_neighbor(board, m->coord) {
		if (board_at(board, c) == m->color && group_at(board, c) != gid) {
			if (gid <= 0) {
				gid = group_at(board, c);
			} else {
				/* Merge groups */
				foreach_in_group(board, group_at(board, c)) {
					group_add(board, gid, c);
				} foreach_in_group_end;
			}
		} else if (board_at(board, c) == stone_other(m->color)
			   && board_group_libs(board, group_at(board, c)) == 1) {
			/* We just filled last liberty of a group in atari. */
			board_group_capture(board, group_at(board, c));
		}
	} foreach_neighbor_end;

	if (gid <= 0) {
		if (gi_allocsize(board->last_gid + 1) < gi_allocsize(board->last_gid + 2)) {
			board->gi = realloc(board->gi, gi_allocsize(board->last_gid + 2) * sizeof(*board->gi));
		}
		gid = ++board->last_gid;
		memset(&board->gi[gid], 0, sizeof(*board->gi));
	}
	group_add(board, gid, m->coord);

record:
	board->last_move = *m;
	board->moves++;

	return gid;
}

int
board_play(struct board *board, struct move *m)
{
	return board_play_raw(board, m, true);
}

bool
board_no_valid_moves(struct board *board, enum stone color)
{
	foreach_point(board) {
		struct move m;
		m.coord.x = x; m.coord.y = y; m.color = color;
		/* Self-atari doesn't count. :-) */
		if (board_valid_move(board, &m, true))
			return false;
	} foreach_point_end;
	return true;
}

bool
board_valid_move(struct board *board, struct move *m, bool sensible)
{
	struct board b2;

	if (is_pass(m->coord) || is_resign(m->coord))
		return true;

	if (board_at(board, m->coord) != S_NONE)
		return false;

	/* Check ko */
	if (m->coord.x == board->last_move.coord.x && m->coord.y == board->last_move.coord.y)
		return false;

	/* Try it! */
	board_copy_on_stack(&b2, board);
	board_play_raw(&b2, m, false);
	if (board_group_libs(&b2, group_at((&b2), m->coord)) <= sensible) {
		/* oops, suicide (or self-atari if sensible) */
		return false;
	}

	return true;
}


int
board_local_libs(struct board *board, struct coord *coord)
{
	int l = 0;

	foreach_neighbor(board, *coord) {
		if (board->libcount_watermark) {
			/* If we get called in loop, our caller can prevent us
			 * from counting some liberties multiple times. */
			if (board->libcount_watermark[c.x + board->size * c.y])
				continue;
			board->libcount_watermark[c.x + board->size * c.y] = true;
		}
		l += (board_at(board, c) == S_NONE);
	} foreach_neighbor_end;
	return l;
}


int
board_group_libs_recount(struct board *board, int group)
{
	int l = 0;
	bool watermarks[board->size * board->size];
	memset(watermarks, 0, sizeof(watermarks));

	board->libcount_watermark = watermarks;

	foreach_in_group(board, group) {
		l += board_local_libs(board, &c);
	} foreach_in_group_end;

	board->libcount_watermark = NULL;

	board_group_libs(board, group) = l;
	return l;
}

void
board_group_capture(struct board *board, int group)
{
	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_at(board, c) = S_NONE;
		group_at(board, c) = 0;
	} foreach_in_group_end;
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
		if (board_at(board, c) != S_NONE) {
			/* There is a complication: There can be some dead
			 * stones that could not have been removed because
			 * they are in enemy territory and we can't suicide.
			 * At least we know they are in atari. */
			int g = group_at(board, c);
			if (gcache[g] == GC_DUNNO)
				gcache[g] = board_group_libs(board, g) == 1 ? GC_DEAD : GC_ALIVE;
			if (gcache[g] == GC_ALIVE)
				scores[board_at(board, c)]++;
#if 0
			This simply is not good enough.
		} else {
			/* Our opponent was sloppy. We try to just look at
			 * our neighbors and as soon as we see a live group
			 * we are set. We always see one or we would already
			 * play on the empty place ourselves. */
			/* The trouble is that sometimes our opponent won't even
			 * bother to atari our group and then we get it still
			 * wrong. */
			foreach_neighbor(board, c) {
				if (board_at(board, c) == S_NONE)
					continue;
				int g = group_at(board, c);
				if (gcache[g] == GC_DUNNO)
					gcache[g] = board_group_libs(board, g) == 1 ? GC_DEAD : GC_ALIVE;
				if (gcache[g] == GC_ALIVE) {
					scores[board_at(board, c)]++;
					break;
				}
			} foreach_neighbor_end;
#endif
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
		scores[board_at(board, c)]++;
	} foreach_point_end;

	return board->komi + scores[S_WHITE] - scores[S_BLACK];
}
