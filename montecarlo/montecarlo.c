#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "montecarlo/montecarlo.h"


/* This is simple monte-carlo engine. For each possible play on current board,
 * it plays MC_GAMES random games from that board; the move with the biggest
 * number of winning games gets played. */


#if 0 // board 19
#define MC_GAMES	1000
#define MC_GAMELEN	400
#else
#define MC_GAMES	200
#define MC_GAMELEN	150
#endif


/* Stolen from the random engine. */
static void
random_move(struct board *b, enum stone color, struct coord *coord)
{
	struct move m;
	m.color = color;

	if (board_no_valid_moves(b, color)) {
		*coord = pass;
		return;
	}

	do {
		m.coord.x = random() % b->size;
		m.coord.y = random() % b->size;
	} while (!board_valid_move(b, &m, true));

	*coord = m.coord;
}

static float
play_random_game(struct board *b, enum stone color, int moves)
{
	struct board b2;
	board_copy(&b2, b);
	
	struct coord coord;
	int passes = 0;
	while (moves-- && passes < 2) {
		random_move(&b2, color, &coord);
		struct move m = { coord, color };
		//char *cs = coord2str(coord); fprintf(stderr, "%s %s\n", stone2str(color), cs); free(cs);
		board_play_nocheck(&b2, &m);
		if (is_pass(coord))
			passes++;
		else
			passes = 0;
		color = stone_other(color);
	}

	return board_fast_score(&b2);
}

/* positive: player-to-play wins more, negative: player-to-play loses more */
static int
play_many_random_games_after(struct board *b, struct move *m)
{
	struct board b2;
	board_copy(&b2, b);
	board_play_nocheck(&b2, m);

	int gamelen = MC_GAMELEN - b2.moves;
	if (gamelen < 10)
		gamelen = 10;

	int balance = 0;

	int i;
	for (i = 0; i < MC_GAMES; i++) {
		float score = play_random_game(&b2, stone_other(m->color), gamelen);
		//fprintf(stderr, "--- game result: %f\n", score);
		balance += (score > 0 ? 1 : -1);
	}

	return balance;
}


static struct coord *
montecarlo_genmove(struct board *b, enum stone color)
{
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

		//fprintf(stderr, "[%d,%d] random\n", x, y);
		int score = -play_many_random_games_after(b, &m);
		//fprintf(stderr, "\tscore %d\n", score);
		if (score > top_score) {
			top_score = score;
			top_coord = m.coord;
		}
	} foreach_point_end;

	return coord_copy(top_coord);
}

struct engine *
engine_montecarlo_init(void)
{
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "MonteCarlo Engine";
	e->comment = "I'm playing in Monte Carlo. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = montecarlo_genmove;
	return e;
}
