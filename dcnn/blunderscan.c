#define DEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "dcnn/dcnn.h"
#include "pattern/pattern.h"
#include "dcnn/blunderscan.h"

/* Monitor play commands and run dcnn blunder checks on all game moves.
 * Feed it entire gamelogs as gtp stream to check all positions.  */


/* Fake dcnn output for speed */
#define BLUNDERSCAN_FAKE_DCNN	1

/* better accuracy on atari patterns matching ?  (will be much slower) */
//#define BLUNDERSCAN_SLOW_ACCURATE	1


static char *
blunderscan_play(engine_t *e, board_t *board, move_t *m, char *enginearg, bool *print_board)
{
	board_t board2;  board_copy(&board2, board);
	board_t *b = &board2;	
	
	if (board_play(b, m) < 0) {
		fprintf(stderr, "! INVALID MOVE %s %s\n", stone2str(m->color), coord2sstr(m->coord));
		board_print(b, stderr);
		die("blunderscan: invalid move\n");
	}
	
	enum stone color = board_to_play(b);

	/* Get ownermap */
	ownermap_t ownermap;
#ifdef BLUNDERSCAN_SLOW_ACCURATE
	mcowner_playouts(b, color, &ownermap);
#else
	mcowner_playouts_fast(b, color, &ownermap);
#endif

	/* Get dcnn output */
	float result[19 * 19];	
#ifdef BLUNDERSCAN_FAKE_DCNN
	for (int i = 0; i < 19 * 19; i++)  /* fake dcnn output */
		result[i] = 0.015;   /* all moves 1.5%   (less than 2% so we can test boosted move trimming) */
#else
	dcnn_evaluate_raw(b, color, result, &ownermap, DEBUGL(2));
#endif

	/* dcnn blunder code */
	dcnn_fix_blunders(b, color, result, &ownermap, DEBUGL(2));	
	
	return NULL;
}

static coord_t
blunderscan_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	die("genmove command not available in blunderscan\n");
}

void
blunderscan_engine_init(engine_t *e, board_t *b)
{
	e->name = "BlunderScan";
	e->comment = "You cannot play Pachi with this engine, it is for debugging purposes.";
	e->genmove = blunderscan_genmove;
	e->notify_play = blunderscan_play;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;

#ifdef BLUNDERSCAN_FAKE_DCNN
	fprintf(stderr, "blunderscan: faking dcnn output\n");
#else
	dcnn_init(b);
#endif
		
	pattern_config_t pc;
	patterns_init(&pc, NULL, false, true);
}
