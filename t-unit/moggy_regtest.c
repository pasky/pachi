#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "playout.h"
#include "playout/moggy.h"

static void
play_game(playout_setup_t *setup,
	  board_t *b, enum stone color,
	  playout_policy_t *policy)
{
	if (policy->setboard)  policy->setboard(policy, b);
	
	int passes = 0;
	for (int gamelen = setup->gamelen; gamelen-- > 0 && passes < 2; ) {
		coord_t coord = playout_play_move(setup, b, color, policy);
		fprintf(stderr, "move %-3i %1.1s %s\n", b->moves, stone2str(color), coord2sstr(coord));
		if (is_pass(coord)) passes++;  else passes = 0;
		color = stone_other(color);
	}	
}

/* Play some predictable moggy games dumping every move. */
bool
moggy_regression_test(board_t *board, char *arg)
{
	int games = 10;
	fast_srandom(0x12345);
	
	if (DEBUGL(2))  board_print(board, stderr);
	if (DEBUGL(1))  printf("moggy regression test.   Playing %i games\n", games);

	playout_policy_t *policy = playout_moggy_init(NULL, board);
	playout_setup_t setup = { .gamelen = MAX_GAMELEN };
	
	/* Play some games */
	for (int i = 0; i < games; i++)  {
		enum stone color = S_BLACK;
		board_t b;
		board_copy(&b, board);
		fprintf(stderr, "game %i:\n", i+1);
		play_game(&setup, &b, color, policy);
		board_done_noalloc(&b);
	}
	
	printf("All good.\n\n");
	return true;
}
