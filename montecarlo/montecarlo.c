#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "montecarlo/montecarlo.h"
#include "random.h"


/* This is simple monte-carlo engine. It plays MC_GAMES random games from the
 * current board and records win/loss ratio for each first move. The move with
 * the biggest number of winning games gets played. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * debug[=DEBUG_LEVEL]		1 is the default; more means more debugging prints
 * games=MC_GAMES		number of random games to play
 * gamelen=MC_GAMELEN		maximal length of played random game
 * domainrate=MC_DOMAINRATE	how many of 10 moves should be non-random but hinted
 * 				by heuristics (currently taking care of atari and
 * 				assuming local responses);
 * 				set to 0 to have purely rules-driven MC search;
 * 				default 5; carries high performance penalty!
 */


#define MC_GAMES	40000
#define MC_GAMELEN	400
#define MC_DOMAINRATE	5


struct montecarlo {
	int debug_level;
	int games, gamelen;
	int domain_rate;
	float resign_ratio;
	int loss_threshold;
};


/* *** Domain-specific knowledge comes here (that is, any heuristics that perfer
 * certain moves, aside of requiring the moves to be according to the rules. */

static coord_t
domain_hint_atari(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* If we or our neighbors are in atari, fix that, (Capture or escape.)
	 * This test costs a lot of performance (the whole playout is about 1/4
	 * slower), but improves the playouts a lot. */

	if (unlikely(mc->debug_level > 8)) {
		fprintf(stderr, "-- Scanning for %d,%d-urgent moves:\n", coord_x(coord), coord_y(coord));
		board_print(b, stderr);
	}

	coord_t urgents[5]; int urgents_len = 0;
	memset(urgents, 0, sizeof(urgents));

	coord_t fix;
	if (unlikely(board_group_in_atari(b, group_at(b, coord), &fix)))
		urgents[urgents_len++] = fix;
	foreach_neighbor(b, coord) {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_in_atari(b, group_at(b, c), &fix)))
			urgents[urgents_len++] = fix;
	} foreach_neighbor_end;

	if (unlikely(urgents_len)) {
		if (unlikely(mc->debug_level > 8)) {
			fprintf(stderr, "Urgent moves found:");
			int i = 0;
			for (i = 0; i < urgents_len; i++)
				fprintf(stderr, " %d,%d", coord_x(urgents[i]), coord_y(urgents[i]));
			fprintf(stderr, "\n");
		}
		return urgents[fast_random(urgents_len)];
	}
	return pass;
}

static coord_t
domain_hint_local(struct montecarlo *mc, struct board *b, coord_t coord)
{
	/* Pick a suitable move that is directly or diagonally adjecent. In the
	 * real game, local moves often tend to be the urgent ones, even if they
	 * aren't atari. */
	/* Note that this test is about as expensive as the atari test. (Maybe
	 * slightly cheaper.) */

	if (unlikely(mc->debug_level > 8)) {
		fprintf(stderr, "-- Scanning for %d,%d-local moves:\n", coord_x(coord), coord_y(coord));
		board_print(b, stderr);
	}

	coord_t neis[S_MAX][8]; int neis_len[S_MAX];
	memset(neis, 0, sizeof(neis));
	memset(neis_len, 0, sizeof(neis_len));

	foreach_neighbor(b, coord) {
		neis[(enum stone) board_at(b, c)][neis_len[(enum stone) board_at(b, c)]++] = c;
	} foreach_neighbor_end;

	foreach_diag_neighbor(b, coord) {
		neis[(enum stone) board_at(b, c)][neis_len[(enum stone) board_at(b, c)]++] = c;
	} foreach_diag_neighbor_end;

	if (likely(neis_len[S_NONE])) {
		if (unlikely(mc->debug_level > 8)) {
			fprintf(stderr, "Local moves found:");
			int i = 0;
			for (i = 0; i < neis_len[S_NONE]; i++)
				fprintf(stderr, " %d,%d", coord_x(neis[S_NONE][i]), coord_y(neis[S_NONE][i]));
			fprintf(stderr, "\n");
		}
		return neis[S_NONE][fast_random(neis_len[S_NONE])];
	}
	return pass;
}


/* 1: m->color wins, 0: m->color loses; -1: no moves left */
static int
play_random_game(struct montecarlo *mc, struct board *b, struct move *m, bool *suicide, int i)
{
	struct board b2;
	board_copy(&b2, b);

	board_play_random(&b2, m->color, &m->coord);
	if (is_pass(m->coord)) {
		if (mc->debug_level > 3)
			fprintf(stderr, "\tno moves left\n");
		board_done_noalloc(&b2);
		return -1;
	}
	*suicide = !group_at(&b2, m->coord);
	if (mc->debug_level > 4 && *suicide) {
		fprintf(stderr, "SUICIDE DETECTED at %d,%d:\n", coord_x(m->coord), coord_y(m->coord));
		board_print(&b2, stderr);
	}

	if (mc->debug_level > 3)
		fprintf(stderr, "[%d,%d] playing random game\n", coord_x(m->coord), coord_y(m->coord));

	int gamelen = mc->gamelen - b2.moves;
	if (gamelen < 10)
		gamelen = 10;

	enum stone color = stone_other(m->color);
	coord_t urgent = pass;

	int passes = 0;
	while (gamelen-- && passes < 2) {
		coord_t coord;

		if (!is_pass(urgent)) {
			struct move m = { urgent, color };
			if (board_play(&b2, &m) < 0) {
				if (unlikely(mc->debug_level > 7)) {
					fprintf(stderr, "Urgent move %d,%d is ILLEGAL:\n", coord_x(urgent), coord_y(urgent));
					board_print(&b2, stderr);
				}
				goto play_random;
			}
		} else {
play_random:
			board_play_random(&b2, color, &coord);
		}

		if (unlikely(mc->debug_level > 7)) {
			char *cs = coord2str(coord);
			fprintf(stderr, "%s %s\n", stone2str(color), cs);
			free(cs);
		}

		if (unlikely(is_pass(coord))) {
			passes++;
		} else {
			passes = 0;

			/* In 1/2 of the cases, we pick one of the urgent moves
			 * instead of a completely random one. */
			urgent = pass;
			if (mc->domain_rate && fast_random(10) < mc->domain_rate) {
				urgent = domain_hint_atari(mc, &b2, coord);
			}
			/* For the non-urgent moves, 1/2 will be contact play
			 * (tsuke or diagonal). These tend to be most likely. */
			if (is_pass(urgent) && mc->domain_rate && fast_random(10) < mc->domain_rate) {
				urgent = domain_hint_local(mc, &b2, coord);
			}
		}

		color = stone_other(color);
	}

	if (mc->debug_level > 6 - !(i % (mc->games/2)))
		board_print(&b2, stderr);

	float score = board_fast_score(&b2);
	if (mc->debug_level > 5 - !(i % (mc->games/2)))
		fprintf(stderr, "--- game result: %f\n", score);

	board_done_noalloc(&b2);
	return (m->color == S_WHITE ? (score > 0 ? 1 : 0) : (score < 0 ? 1 : 0));
}


static coord_t *
montecarlo_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct montecarlo *mc = e->data;
	struct move m;
	m.color = color;

	/* resign when the hope for win vanishes */
	coord_t top_coord = resign;
	float top_ratio = mc->resign_ratio;

	int games[b->size * b->size];
	int wins[b->size * b->size];
	bool suicides[b->size * b->size];
	memset(games, 0, sizeof(games));
	memset(wins, 0, sizeof(wins));
	memset(suicides, 0, sizeof(suicides));

	int losses = 0;
	int i;
	for (i = 0; i < mc->games; i++) {
		bool suicide = false;
		int result = play_random_game(mc, b, &m, &suicide, i);
		if (result < 0) {
pass_wins:
			/* No more moves. */
			top_coord = pass; top_ratio = 0.5;
			goto move_found;
		}

		if (mc->debug_level > 3)
			fprintf(stderr, "\tresult %d\n", result);

		games[m.coord.pos]++;

		if (b->moves < 3) {
			/* Simple heuristic: avoid opening too low. Do not
			 * play on second or first line as first white or
			 * first two black moves.*/
			if (coord_x(m.coord) < 3 || coord_x(m.coord) > b->size - 4
			    || coord_y(m.coord) < 3 || coord_y(m.coord) > b->size - 4)
				continue;
		}

		losses += 1 - result;
		wins[m.coord.pos] += result;
		suicides[m.coord.pos] = suicide;

		if (unlikely(!losses && i == mc->loss_threshold)) {
			/* We played out many games and didn't lose once yet.
			 * This game is over. */
			break;
		}
	}

	bool suicide_candidate = false;
	foreach_point(b) {
		float ratio = (float) wins[c.pos] / games[c.pos];
		if (ratio > top_ratio) {
			if (ratio == 1 && suicides[c.pos]) {
				if (mc->debug_level > 2)
					fprintf(stderr, "not playing suicide at %d,%d\n", coord_x(top_coord), coord_y(top_coord));
				suicide_candidate = true;
				continue;
			}
			top_ratio = ratio;
			top_coord = c;
		}
	} foreach_point_end;
	if (is_resign(top_coord) && suicide_candidate) {
		/* The only possibilities now are suicides. */
		goto pass_wins;
	}

	if (mc->debug_level > 2) {
		struct board *board = b;
		FILE *f = stderr;
		fprintf(f, "\n       ");
		int x, y;
		char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
		for (x = 1; x < board->size - 1; x++)
			fprintf(f, "%c    ", asdf[x - 1]);
		fprintf(f, "\n   +-");
		for (x = 1; x < board->size - 1; x++)
			fprintf(f, "-----");
		fprintf(f, "+\n");
		for (y = board->size - 2; y >= 1; y--) {
			fprintf(f, "%2d | ", y);
			for (x = 1; x < board->size - 1; x++)
				if (games[y * board->size + x])
					fprintf(f, "%0.2f ", (float) wins[y * board->size + x] / games[y * board->size + x]);
				else
					fprintf(f, "---- ");
			fprintf(f, "| ");
			for (x = 1; x < board->size - 1; x++)
				fprintf(f, "%4d ", games[y * board->size + x]);
			fprintf(f, "|\n");
		}
		fprintf(f, "   +-");
		for (x = 1; x < board->size - 1; x++)
			fprintf(f, "-----");
		fprintf(f, "+\n");
	}

move_found:
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
	e->comment = "I'm playing in Monte Carlo. When we both pass, I will consider all the stones on the board alive. Please bear with me at the game end, I need to fill the whole board; if you help me, we will both be happier. Filling the board will not lose points (NZ rules).";
	e->genmove = montecarlo_genmove;
	e->data = mc;

	mc->debug_level = 1;
	mc->games = MC_GAMES;
	mc->gamelen = MC_GAMELEN;
	mc->domain_rate = MC_DOMAINRATE;

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
			} else if (!strcasecmp(optname, "domainrate") && optval) {
				mc->domain_rate = atoi(optval);
			} else {
				fprintf(stderr, "MonteCarlo: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	mc->resign_ratio = 0.1; /* Resign when most games are lost. */
	mc->loss_threshold = mc->games / 10; /* Stop reading if no loss encountered in first n games. */

	return e;
}
