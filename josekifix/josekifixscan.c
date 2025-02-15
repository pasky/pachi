#define DEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "josekifix/joseki_override.h"
#include "josekifix/josekifixscan.h"
#include "josekifix/josekifix_engine.h"

/* Monitor play commands and check for josekifix pattern matches on all game moves.
 * Feed it entire gamelogs as gtp stream to check all positions.  */

static char *
josekifixscan_play(engine_t *e, board_t *board, move_t *m, char *enginearg, bool *print_board)
{
	board_t board2;  board_copy(&board2, board);
	board_t *b = &board2;

	/* Copy history, needed for fuseki matches */
	move_history_t history;
	assert(board->move_history);
	memcpy(&history, board->move_history, sizeof(history));
	b->move_history = &history;
	
	if (board_play(b, m) < 0) {
		fprintf(stderr, "! INVALID MOVE %s %s\n", stone2str(m->color), coord2sstr(m->coord));
		board_print(b, stderr);
		die("josekifixscan: invalid move\n");
	}
	
	/* Print board on matches */
	coord_t c = joseki_override(b);	
	if (!is_pass(c))
		board_print(b, stderr);
	
	return NULL;
}

static enum parse_code
josekifixscan_notify(engine_t *e, board_t *b, int id, char *cmd, char *args, gtp_t *gtp)
{
	/* Skip final_status_list commands. */
	if (!strcmp(cmd, "final_status_list"))
		return P_DONE_OK;

	return P_OK;
}

static coord_t
josekifixscan_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	die("genmove command not available in josekifixscan\n");
}

void
josekifixscan_engine_init(engine_t *e, board_t *b)
{
	e->name = "JosekifixScan";
	e->comment = "You cannot play Pachi with this engine, it is for debugging purposes.";
	e->genmove = josekifixscan_genmove;
	e->notify_play = josekifixscan_play;
	e->notify = josekifixscan_notify;
	// Don't reset engine on clear_board or undo.
	e->keep_on_clear = true;
	e->keep_on_undo = true;

	/* Sanity checks */
	assert(b->rsize == 19);
	require_josekifix();
	
	if (!get_josekifix_enabled())
		die("Can't run josekifixscan engine with josekifix disabled.\n");

	/* Fake external engine */
	set_fake_external_joseki_engine();

	/* Load josekifix database */
	if (!josekifix_init(b))
		die("Couldn't load josekifix data\n");
}
