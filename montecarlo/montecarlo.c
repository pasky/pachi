#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "montecarlo/montecarlo.h"


/* This is simple monte-carlo engine. For each possible play on current board,
 * it plays MC_GAMES random games from that board; the move with the biggest
 * number of winning games gets played. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * debug=DEBUG_LEVEL
 * games=MC_GAMES
 * gamelen=MC_GAMELEN
 */


#if 0 // board 19
#define MC_GAMES	1000
#define MC_GAMELEN	400
#else
#define MC_GAMES	1000
#define MC_GAMELEN	150
#endif


struct montecarlo {
	int debug_level;
	int games, gamelen;
};


/* Stolen from the random engine. */
static void
random_move(struct montecarlo *mc, struct board *b, enum stone color, struct coord *coord)
{
#define TRIES	MC_GAMELEN
	struct move m;
	m.color = color;
	int tries = 0;

	/* board_no_valid_moves() is too expensive for us so we just try
	 * a "few" times and then give up. */

	do {
		m.coord.x = random() % b->size;
		m.coord.y = random() % b->size;
	} while ((board_at(b, m.coord) != S_NONE /* common case */
	          || !board_valid_move(b, &m, true))
		 && tries++ < TRIES);

	if (tries <= TRIES)
		*coord = m.coord;
	else
		*coord = pass;
#undef TRIES
}

static float
play_random_game(struct montecarlo *mc, struct board *b, enum stone color, int moves)
{
	struct board b2;
	board_copy(&b2, b);
	
	struct coord coord;
	int passes = 0;
	while (moves-- && passes < 2) {
		random_move(mc, &b2, color, &coord);
		struct move m = { coord, color };
		if (mc->debug_level > 6) {
			char *cs = coord2str(coord);
			fprintf(stderr, "%s %s\n", stone2str(color), cs);
			free(cs);
		}
		board_play_nocheck(&b2, &m);
		if (is_pass(coord))
			passes++;
		else
			passes = 0;
		color = stone_other(color);
	}

	if (mc->debug_level > 5)
		board_print(&b2, stderr);

	float score = board_fast_score(&b2);

	board_done_noalloc(&b2);
	return score;
}

/* positive: player-to-play wins more, negative: player-to-play loses more */
static int
play_many_random_games_from(struct montecarlo *mc, struct board *b, struct move *m)
{
	struct board b2;
	board_copy(&b2, b);
	board_play_nocheck(&b2, m);

	int gamelen = mc->gamelen - b2.moves;
	if (gamelen < 10)
		gamelen = 10;

	int balance = 0;

	int i;
	for (i = 0; i < mc->games; i++) {
		float score = play_random_game(mc, &b2, stone_other(m->color), gamelen);
		if (mc->debug_level > 4)
			fprintf(stderr, "--- game result: %f\n", score);
		balance += (score > 0 ? 1 : -1) * (stone_other(m->color) == S_WHITE ? 1 : -1);
	}

	board_done_noalloc(&b2);
	return balance;
}


static struct coord *
montecarlo_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct montecarlo *mc = e->data;
	struct move m;
	m.color = color;

	if (board_no_valid_moves(b, color))
		return coord_pass();

	/* pass is better than playing a losing move. */
	struct coord top_coord = pass;
	int top_score = 0;

	foreach_point(b) {
		m.coord = c;
		if (!board_valid_move(b, &m, true))
			continue;

		if (mc->debug_level > 3)
			fprintf(stderr, "[%d,%d] random games\n", x, y);
		int score = - play_many_random_games_from(mc, b, &m);
		if (mc->debug_level > 3)
			fprintf(stderr, "\tscore %d\n", score);
		if (score > top_score) {
			top_score = score;
			top_coord = m.coord;
		}
	} foreach_point_end;

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

	return e;
}
