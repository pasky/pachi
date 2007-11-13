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
		(b2)->use_alloca = true; \
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
			if (coord_x(board->last_move.coord) == x && coord_y(board->last_move.coord) == y)
				fprintf(f, "%c)", stone2char(board_atxy(board, x, y)));
			else
				fprintf(f, "%c ", stone2char(board_atxy(board, x, y)));
		}
		if (unlikely(debug_level > 6)) {
			fprintf(f, "| ");
			for (x = 0; x < board->size; x++) {
				fprintf(f, "%d ", board_group_libs(board, group_atxy(board, x, y)));
			}
		}
		fprintf(f, "|\n");
	}
	fprintf(f, "   +-");
	for (x = 0; x < board->size; x++)
		fprintf(f, "--");
	fprintf(f, "+\n\n");
}


static void
group_add(struct board *board, int gid, coord_t coord)
{
	foreach_neighbor(board, coord) {
		if (board_at(board, c) == S_NONE
		    && likely(!board_is_liberty_of(board, &c, gid))) {
			board_group_libs(board, gid)++;
		}
	} foreach_neighbor_end;
	group_at(board, coord) = gid;

	if (unlikely(debug_level > 8))
		fprintf(stderr, "group_add: added %d,%d to group %d - libs %d\n",
			coord_x(coord), coord_y(coord), gid, board_group_libs(board, gid));
}

int
board_play_raw(struct board *board, struct move *m)
{
	struct move ko = { pass, S_NONE };
	int gid = 0;

	board_at(board, m->coord) = m->color;

	int gidls[4], gids = 0;

	foreach_neighbor(board, m->coord) {
		enum stone color = board_at(board, c);
		group_t group = group_at(board, c);

		if (color == S_NONE)
			continue;

		int i;
		for (i = 0; i < gids; i++)
			if (gidls[i] == group)
				goto already_took_liberty;

		gidls[gids++] = group;
		board_group_libs(board, group)--;
		if (unlikely(debug_level > 7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d: libs %d\n",
				group, board_group_libs(board, group));
already_took_liberty:

		if (unlikely(color == m->color) && group != gid) {
			if (likely(gid <= 0)) {
				gid = group;
			} else {
				/* Merge groups */
				if (unlikely(debug_level > 7))
					fprintf(stderr, "board_play_raw: merging groups %d(%d), %d(%d)\n",
						group, board_group_libs(board, group),
						gid, board_group_libs(board, gid));
				foreach_in_group(board, group) {
					group_add(board, gid, c);
				} foreach_in_group_end;
				if (unlikely(debug_level > 7))
					fprintf(stderr, "board_play_raw: merged group: %d(%d)\n",
						gid, board_group_libs(board, gid));
			}
		} else if (unlikely(color == stone_other(m->color))) {
			if (unlikely(board_group_libs(board, group) == 0)) {
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

	if (likely(gid <= 0)) {
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
		gid = ++board->last_gid;
		memset(&board->gi[gid], 0, sizeof(*board->gi));
	}
	group_add(board, gid, m->coord);

	board->last_move = *m;
	board->moves++;
	board->ko = ko;

	return gid;
}

static int
board_check_and_play(struct board *board, struct move *m, bool sensible, bool play)
{
	struct board b2;

	if (unlikely(is_pass(m->coord) || is_resign(m->coord)))
		return -1;

	if (unlikely(board_at(board, m->coord) != S_NONE)) {
		if (unlikely(debug_level > 7))
			fprintf(stderr, "board_check: stone exists\n");
		return 0;
	}

	/* Try it! */
	/* XXX: play == false is kinda rare so we don't bother to optimize that */
	board_copy_on_stack(&b2, board);
	int gid = board_play_raw(board, m);
	if (unlikely(debug_level > 7))
		fprintf(stderr, "board_play_raw(%d,%d,%d): %d\n", m->color, coord_x(m->coord), coord_y(m->coord), gid);

	int my_libs = board_group_libs(board, group_at(board, m->coord));
	if (unlikely(my_libs <= sensible)) {
		/* oops, suicide (or self-atari if sensible) */
		if (unlikely(debug_level > 5))
			fprintf(stderr, "suicide: libs %d <= sens %d\n",
				board_group_libs(board, group_at(board, m->coord)), sensible);
		gid = 0; play = false;
	}

	/* Check ko: self-atari one-stone capture at a position of one-stone capture one move ago (thus b2, not board !) */
	if (unlikely(my_libs == 1 && m->color == b2.ko.color && coord_eq(m->coord, b2.ko.coord) && board->captures[m->color] - b2.captures[m->color] == 1)) {
		if (unlikely(debug_level > 5))
			fprintf(stderr, "board_check: ko at %d,%d color %d captures %d-%d\n", coord_x(m->coord), coord_y(m->coord), m->color, board->captures[m->color], b2.captures[m->color]);
		gid = 0; play = false;
	}

	if (unlikely(!play)) {
		/* Restore the original board. */
		void *b = board->b, *g = board->g, *gi = board->gi;
		memcpy(board->b, b2.b, b2.size * b2.size * sizeof(*b2.b));
		memcpy(board->g, b2.g, b2.size * b2.size * sizeof(*b2.g));
		memcpy(board->gi, b2.gi, (b2.last_gid + 1) * sizeof(*b2.gi));
		memcpy(board, &b2, sizeof(b2));
		board->b = b; board->g = g; board->gi = gi;
		board->use_alloca = false;
	}

	return gid;
}

int
board_play(struct board *board, struct move *m)
{
	return board_check_and_play(board, m, false, true);
}

bool
board_valid_move(struct board *board, struct move *m, bool sensible)
{
	return board_check_and_play(board, m, sensible, false);
}

bool
board_no_valid_moves(struct board *board, enum stone color)
{
	foreach_point(board) {
		struct move m;
		m.coord = c; m.color = color;
		/* Self-atari doesn't count. :-) */
		if (board_valid_move(board, &m, true) && !board_is_one_point_eye(board, &m.coord))
			return false;
	} foreach_point_end;
	return true;
}


static inline bool
board_is_sensible_move(struct board *b, struct move *m, int i)
{
	m->coord.x = i % b->size;
	m->coord.y = i / b->size;
	return (board_at(b, m->coord) == S_NONE
	        && board_is_one_point_eye(b, &m->coord) != m->color /* bad idea, usually */
	        && board_play(b, m));
}

void
board_play_random(struct board *b, enum stone color, coord_t *coord)
{
	struct move m;
	m.color = color;

	int ofs = random() % (b->size * b->size);
	int i;
	for (i = ofs; i < b->size * b->size; i++)
		if (board_is_sensible_move(b, &m, i))
			goto found_move;
	for (i = 0; i < ofs; i++)
		if (board_is_sensible_move(b, &m, i))
			goto found_move;

	m.coord = pass;

found_move:
	*coord = m.coord;
}


bool
board_is_liberty_of(struct board *board, coord_t *coord, int group)
{
	foreach_neighbor(board, *coord) {
		if (unlikely(group_at(board, c) == group))
			return true;
	} foreach_neighbor_end;
	return false;
}

enum stone
board_is_one_point_eye(struct board *board, coord_t *coord)
{
	enum stone eye_color = S_NONE;

	foreach_neighbor(board, *coord) {
		enum stone color = board_at(board, c);

		if (color == S_NONE)
			return S_NONE;
		if (eye_color != S_NONE && color != eye_color)
			return S_NONE;
		eye_color = color;

		/* But there's a catch - false eye. To make things simple,
		 * we aren't an eye if at least one of the neighbors is
		 * in atari. That should be good enough. */
		if (board_group_libs(board, group_at(board, c)) == 1)
			return S_NONE;
	} foreach_neighbor_end;

	return eye_color;
}


int
board_group_capture(struct board *board, int group)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_at(board, c) = S_NONE;
		group_at(board, c) = 0;
		stones++;

		/* Increase liberties of surrounding groups */
		coord_t coord = c;
		int gidls[4], gids = 0;
		foreach_neighbor(board, coord) {
			if (board_at(board, c) != S_NONE) {
				int gid = group_at(board, c);
				if (gid == group)
					goto next_neighbor; /* Not worth the trouble */

				/* Do not add a liberty twice to one group. */
				int i;
				for (i = 0; i < gids; i++)
					if (unlikely(gidls[i] == gid))
						goto next_neighbor;
				gidls[gids++] = gid;

				board_group_libs(board, gid)++;
			}
next_neighbor:;
		} foreach_neighbor_end;
	} foreach_in_group_end;

	return stones;
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
		if (color != S_NONE) {
			/* There is a complication: There can be some dead
			 * stones that could not have been removed because
			 * they are in enemy territory and we can't suicide.
			 * At least we know they are in atari. */
			int g = group_at(board, c);
			if (gcache[g] == GC_DUNNO)
				gcache[g] = board_group_libs(board, g) == 1 ? GC_DEAD : GC_ALIVE;
			if (gcache[g] == GC_ALIVE)
				scores[color]++;
			/* XXX: But we still miss the one empty opponent's point. */

		} else {
			/* TODO: Count multi-point eyes */
			color = board_is_one_point_eye(board, &c);
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
			color = board_is_one_point_eye(board, &c);
		scores[color]++;
	} foreach_point_end;

	return board->komi + scores[S_WHITE] - scores[S_BLACK];
}
