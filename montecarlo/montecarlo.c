#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "montecarlo/montecarlo.h"


/* This is simple monte-carlo engine. It plays MC_GAMES random games from the
 * current board and records win/loss ratio for each first move. The move with
 * the biggest number of winning games gets played. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * debug[=DEBUG_LEVEL]		1 is the default; more means more debugging prints
 * games=MC_GAMES		number of random games to play
 * gamelen=MC_GAMELEN		maximal length of played random game
 */


#define MC_GAMES	40000
#define MC_GAMELEN	400


struct montecarlo {
	int debug_level;
	int games, gamelen;
	float resign_ratio;
};


static float
play_random_game(struct montecarlo *mc, struct board *b, enum stone color, int moves, int n)
{
	struct board b2;
	board_copy(&b2, b);
	
	coord_t coord;
	int passes = 0;
	while (moves-- && passes < 2) {
		board_play_random(&b2, color, &coord);
		if (mc->debug_level > 7) {
			char *cs = coord2str(coord);
			fprintf(stderr, "%s %s\n", stone2str(color), cs);
			free(cs);
		}
		if (is_pass(coord))
			passes++;
		else
			passes = 0;
		color = stone_other(color);
	}

	if (mc->debug_level > 6 - !(n % (mc->games/2)))
		board_print(&b2, stderr);

	float score = board_fast_score(&b2);

	board_done_noalloc(&b2);
	return score;
}

/* 1: player-to-play wins, 0: player-to-play loses; -1: invalid move */
static int
play_random_game_from(struct montecarlo *mc, struct board *b, struct move *m, int i)
{
	if (board_is_one_point_eye(b, &m->coord) == m->color)
		/* Invalid move */
		return -1;

	struct board b2;
	board_copy(&b2, b);
	if (!board_play(&b2, m)) {
		/* Invalid move */
		board_done_noalloc(&b2);
		return -1;
	}

	int gamelen = mc->gamelen - b2.moves;
	if (gamelen < 10)
		gamelen = 10;

	float score = play_random_game(mc, &b2, stone_other(m->color), gamelen, i);
	if (mc->debug_level > 5 - !(i % (mc->games/2)))
		fprintf(stderr, "--- game result: %f\n", score);

	board_done_noalloc(&b2);
	return (stone_other(m->color) == S_WHITE ? (score > 0 ? 1 : 0) : (score < 0 ? 1 : 0));
}


static coord_t *
montecarlo_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct montecarlo *mc = e->data;
	struct move m;
	m.color = color;

	if (board_no_valid_moves(b, color))
		return coord_pass();

	/* resign when the hope for win vanishes */
	coord_t top_coord = resign;
	float top_ratio = mc->resign_ratio;

	int moves = 0;

	int games[b->size][b->size];
	int wins[b->size][b->size];
	memset(games, 0, sizeof(games));
	memset(wins, 0, sizeof(wins));

	int i;
	for (i = 0; i < mc->games; i++) {
		m.coord.x = random() % b->size;
		m.coord.y = random() % b->size;

		if (mc->debug_level > 3)
			fprintf(stderr, "[%d,%d] playing random game\n", coord_x(m.coord), coord_y(m.coord));

		int result = play_random_game_from(mc, b, &m, i);
		if (result < 0) {
			if (mc->debug_level > 3)
				fprintf(stderr, "\tinvalid move\n");
			continue;
		}
		result = 1 - result; /* We care about whether *we* win. */

		if (mc->debug_level > 3)
			fprintf(stderr, "\tresult %d\n", result);

		games[coord_y(m.coord)][coord_x(m.coord)]++;
		wins[coord_y(m.coord)][coord_x(m.coord)] += result;
		moves++;
	}

	if (!moves) {
		/* Final candidate! But only if we CAN'T make any further move. */
		top_coord = pass; top_ratio = 0.5;

	} else {
		foreach_point(b) {
			float ratio = (float) wins[coord_y(c)][coord_x(c)] / games[coord_y(c)][coord_x(c)];
			if (ratio > top_ratio) {
				top_ratio = ratio;
				top_coord = c;
			}
		} foreach_point_end;
	}

	if (mc->debug_level > 2) {
		struct board *board = b;
		FILE *f = stderr;
		fprintf(f, "\n       ");
		int x, y;
		char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
		for (x = 0; x < board->size; x++)
			fprintf(f, "%c    ", asdf[x]);
		fprintf(f, "\n   +-");
		for (x = 0; x < board->size; x++)
			fprintf(f, "-----");
		fprintf(f, "+\n");
		for (y = board->size - 1; y >= 0; y--) {
			fprintf(f, "%2d | ", y + 1);
			for (x = 0; x < board->size; x++)
				fprintf(f, "%0.2f ", (float) wins[y][x] / games[y][x]);
			fprintf(f, "|\n");
		}
		fprintf(f, "   +-");
		for (x = 0; x < board->size; x++)
			fprintf(f, "-----");
		fprintf(f, "+\n");
	}
	if (mc->debug_level > 1)
		fprintf(stderr, "*** WINNER is %d,%d with score %1.4f\n", coord_x(top_coord), coord_y(top_coord), top_ratio);

	return coord_copy(top_coord);
}

struct engine *
engine_montecarlo_init(char *arg)
{
	struct montecarlo *mc = calloc(1, sizeof(struct montecarlo));
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "MonteCarlo Engine";
	e->comment = "I'm playing in Monte Carlo. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = montecarlo_genmove;
	e->data = mc;

	mc->debug_level = 1;
	mc->games = MC_GAMES;
	mc->gamelen = MC_GAMELEN;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					mc->debug_level = atoi(optval);
				else
					mc->debug_level++;
			} else if (!strcasecmp(optname, "games") && optval) {
				mc->games = atoi(optval);
			} else if (!strcasecmp(optname, "gamelen") && optval) {
				mc->gamelen = atoi(optval);
			} else {
				fprintf(stderr, "MonteCarlo: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	mc->resign_ratio = 0.1; /* Resign when most games are lost. */

	return e;
}
