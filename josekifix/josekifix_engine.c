#define DEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "engines/external.h"
#include "josekifix/josekifix_engine.h"
#include "josekifix/joseki_override.h"
#include "tactics/util.h"
#include "dcnn/dcnn.h"
#include "uct/uct.h"


/* Engine state */
static engine_t  *uct_engine = NULL;
static engine_t  *external_joseki_engine = NULL;
static bool       undo_pending = false;
static bool       fake_external_joseki_engine = false;
static ownermap_t prev_ownermap;

/* Globals */
char *external_joseki_engine_cmd = "katago gtp";
bool  external_joseki_engine_genmoved = false;


/**********************************************************************************************************/
/* UCT engine */

/* If uct had state which depended on knowing all moves played in order,
 * after undo or genmove override we'd need to replay moves like gtp layer
 * does. That's not the case so just reset engine. */
static void
reset_uct_engine(board_t *b)
{
	if (DEBUGL(3)) fprintf(stderr, "Resetting uct engine\n");
	engine_reset(uct_engine, b);
	ownermap_init(&prev_ownermap);
}


/**********************************************************************************************************/
/* External engine */

static bool
start_external_joseki_engine(board_t *b)
{
	assert(!external_joseki_engine);
 
	char *cmd = external_joseki_engine_cmd;
	if (!cmd)  return false;

	strbuf(buf, 1024);
	strbuf_printf(buf, "cmd=%s", cmd);
	engine_t *e = new_engine(E_EXTERNAL, buf->str, b);
	
	if (!external_engine_started(e)) {
		delete_engine(&e);
		return false;
	}

	external_joseki_engine = e;
	return true;
}

void
set_fake_external_joseki_engine(void)
{
	fake_external_joseki_engine = true;
}

coord_t
external_joseki_engine_genmove(board_t *b)
{
	external_joseki_engine_genmoved = true;

	if (fake_external_joseki_engine) {	/* Fake engine ? Return first move available. */
		coord_t c = b->f[0];
		if (DEBUGL(2))  fprintf(stderr, "external joseki engine move: %s  (fake)\n", coord2sstr(c));
		return c;
	}
	
	enum stone color = board_to_play(b);
	coord_t c = external_joseki_engine->genmove(external_joseki_engine, b, NULL, color, false);
	return c;
}


/**********************************************************************************************************/
/* Genmove */

/* Early genmove logic:
 * If there's an override involving external joseki engine we want to avoid spending time in
 * both engines. So check if there's an override involving external engine:
 * - If so get final move from it. uct genmove will be skipped.
 * - Otherwise return pass. Override will be handled after genmove.
 * 
 * Also take care to apply overrides to external engine moves if in external_joseki_engine_mode,
 * they should take precedence. If there's an override still ask it for a move even though we
 * don't need it to keep game timing the same. */
coord_t
joseki_override_before_genmove(board_t *b, enum stone color)
{
	coord_t c = pass;
	int quad = last_quadrant(b);
	bool external_joseki_engine_mode_on = b->external_joseki_engine_moves_left_by_quadrant[quad];
	
	if (external_joseki_engine_mode_on) {
		b->external_joseki_engine_moves_left_by_quadrant[quad]--;
		
		if (DEBUGL(3))  fprintf(stderr, "external joseki engine mode: quadrant %i, moves left: %i\n",
					quad, b->external_joseki_engine_moves_left_by_quadrant[quad]);

		/* First check overrides. */
		c = joseki_override(b);

		/* If genmoved, we have final move and we spent some time thinking, all good.  */
		if (external_joseki_engine_genmoved)  return c;

		/* Get move now then ... */
		coord_t c2 = external_joseki_engine_genmove(b);

		/* But let override take over if different. */
		if (!is_pass(c) && !is_pass(c2) && c2 != c) {   /* Keep engines in sync ! */
			c2 = c;
			external_engine_undo(external_joseki_engine);	/* Undo external engine move, */
			external_joseki_engine_genmoved = false;	/* caller will send play command. */
		}
		
		return c2;
	}
	
	if (is_pass(c))
		c = joseki_override_external_engine_only(b);

	return c;
}

static coord_t
joseki_override_after_genmove(board_t *b, enum stone color, time_info_t *ti, bool pass_all_alive, engine_genmove_t uct_genmove_func)
{
	coord_t c = uct_genmove_func(uct_engine, b, ti, color, pass_all_alive);
	ownermap_t ownermap = *engine_ownermap(uct_engine, b);  // Copy ownermap

	/* Check joseki override, reset uct if necessary. */
	if (!is_pass(c)) {
		coord_t override = joseki_override_no_external_engine(b, &prev_ownermap, &ownermap);
		if (!is_pass(override) && c != override) {
			c = override;
			reset_uct_engine(b);
		}
	}

	/* Save ownermap */
	prev_ownermap = ownermap;
	
	return c;
}


/* Get move from engine, or joseki override if there is one.
 * There are 2 joseki override hooks : one before engine genmove and one after.
 * Without external engine we'd need only the second one, but with 2 engines we
 * want to avoid asking both engines as that would mean a serious delay. So this
 * acts as a dispatch, short-cirtuiting engine genmove when we know it will be
 * overridden by an external engine move. */
static coord_t
genmove(board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive, engine_genmove_t uct_genmove_func)
{
	external_joseki_engine_genmoved = false;
	
	coord_t c = joseki_override_before_genmove(b, color);

	if (is_pass(c))
		c = joseki_override_after_genmove(b, color, ti, pass_all_alive, uct_genmove_func);

	/* Send new move to external engine if it doesn't come from it. */
	if (!is_resign(c) && !external_joseki_engine_genmoved)
		external_engine_play(external_joseki_engine, c, color);
	
	return c;
}

static coord_t
josekifix_engine_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	return genmove(b, ti, color, pass_all_alive, uct_engine->genmove);
}

static coord_t
josekifix_engine_genmove_analyze(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	return genmove(b, ti, color, pass_all_alive, uct_engine->genmove_analyze);
}


/**********************************************************************************************************/
/* UCT plumbing */

/* Forward setoption() calls.
 * Not terribly efficient, the way it works right now we'll reset engine 5 times if
 * there are 5 options that need a reset... */
static bool
josekifix_engine_setoption(engine_t *e, board_t *b, const char *optname, char *optval,
	      char **err, bool setup, bool *caller_reset)
{
	bool reset = false;	/* Use own reset, we don't want caller to ever reset us ! */
	if (!uct_engine->setoption(uct_engine, b, optname, optval, err, setup, &reset) &&
	    !reset)
		return false;
	
	/* Save option */
	engine_options_add(&uct_engine->options, optname, optval);

	/* Engine reset needed ? */
	if (reset)
		reset_uct_engine(b);
	
	return true;
}

static void
josekifix_engine_board_print(engine_t *e, board_t *b, FILE *f)
{
	uct_engine->board_print(uct_engine, b, f);
}

static char *
josekifix_engine_chat(engine_t *e, board_t *b, bool opponent, char *from, char *cmd)
{
	return uct_engine->chat(uct_engine, b, opponent, from, cmd);
}

static char *
josekifix_engine_result(engine_t *e, board_t *b)
{
	return uct_engine->result(uct_engine, b);
}

static void
josekifix_engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
			    coord_t *best_c, float *best_r, int nbest)
{
	uct_engine->best_moves(uct_engine, b, ti, color, best_c, best_r, nbest);
}

void
josekifix_engine_evaluate(engine_t *e, board_t *b, time_info_t *ti, floating_t *vals, enum stone color)
{
	uct_engine->evaluate(uct_engine, b, ti, vals, color);
}

static void
josekifix_engine_analyze(engine_t *e, board_t *b, enum stone color, int start)
{
	uct_engine->analyze(uct_engine, b, color, start);
}

static void
josekifix_engine_dead_groups(engine_t *e, board_t *b, move_queue_t *dead)
{
	uct_engine->dead_groups(uct_engine, b, dead);
}

static void
josekifix_engine_stop(engine_t *e)
{
	uct_engine->stop(uct_engine);
}

static ownermap_t*
josekifix_engine_ownermap(engine_t *e, board_t *b)
{
	return uct_engine->ownermap(uct_engine, b);
}

static char*
josekifix_engine_notify_play(engine_t *e, board_t *b, move_t *m, char *arg, bool *print_board)
{
	return uct_engine->notify_play(uct_engine, b, m, arg, print_board);
}


/**********************************************************************************************************/
/* Notify */

/* Forward commands to external/uct engines. */
static enum parse_code
josekifix_engine_notify(engine_t *e, board_t *b, int id, char *cmd, char *args, gtp_t *gtp)
{
	/* Undo handling: 
	 * external engine takes care of itself, we just forward undo commands.
	 * uct engine however needs to be reset after first non-undo command. */
	if (undo_pending && strcmp(cmd, "undo")) {
		undo_pending = false;
		reset_uct_engine(b);
	}
	if (!strcmp(cmd, "undo"))
		undo_pending = true;

	/* Forward command to external engine. */
	external_joseki_engine->notify(external_joseki_engine, b, id, cmd, args, gtp);
	
	return P_OK;
}

/* Forward commands to uct engine (after gtp handler has run). */
static void
josekifix_engine_notify_after(engine_t *e, board_t *b, int id, char *cmd, gtp_t *gtp)
{
	/* Commands that need uct_engine reset. */
	if (!strcmp(cmd, "clear_board") || !strcmp(cmd, "boardsize"))
		reset_uct_engine(b);
}


/**********************************************************************************************************/
/* Engine init */

static void
josekifix_engine_done(engine_t *e)
{
	delete_engine(&uct_engine);
	delete_engine(&external_joseki_engine);
}

/* Keep in sync with uct_engine_init(). */
void
josekifix_engine_init(engine_t *e, board_t *b)
{
	e->name = "UCT+Josekifix";
	e->comment = "Pachi UCT Monte Carlo Tree Search engine (with joseki fixes)";
	e->keep_on_clear = true;	/* Do not reset engine on clear_board */
	e->keep_on_undo = true;		/* Do not reset engine after undo */

	e->setoption = josekifix_engine_setoption;
	e->board_print = josekifix_engine_board_print;
	e->notify = josekifix_engine_notify;
	e->notify_after = josekifix_engine_notify_after;
	e->notify_play = josekifix_engine_notify_play;
	e->chat = josekifix_engine_chat;
	e->result = josekifix_engine_result;
	e->genmove = josekifix_engine_genmove;
	e->genmove_analyze = josekifix_engine_genmove_analyze;
	e->best_moves = josekifix_engine_best_moves;
	e->evaluate = josekifix_engine_evaluate;
	e->analyze = josekifix_engine_analyze;
	e->dead_groups = josekifix_engine_dead_groups;
	e->stop = josekifix_engine_stop;
	e->ownermap = josekifix_engine_ownermap;

	e->done = josekifix_engine_done;

	ownermap_init(&prev_ownermap);
}


/**********************************************************************************************************/
/* Main call */

/* Return main engine to use:
 * - josekifix engine if joseki fixes are enabled
 * - uct engine       otherwise
 * josekifix engine acts as middle man between gtp and uct engine. */
engine_t*
josekifix_engine_if_needed(engine_t *uct, board_t *b)
{
	uct_engine = uct;

	if (!using_dcnn(b) || uct_is_slave(uct)) {
		disable_josekifix();
		return uct_engine;
	}
	
	if (!get_josekifix_enabled()) {
		if (DEBUGL(2))  fprintf(stderr, "Joseki fixes disabled\n");
		return uct_engine;
	}

	start_external_joseki_engine(b);
	
	/* While we could support a degraded mode where only self-contained overrides are supported
	 * when external engine is missing, joseki fixes database is designed with external engine
	 * in mind and will not play its role without it. Disable joseki fixes and let user know. */	
	if (!external_joseki_engine) {
		if (get_josekifix_required())  die("josekifix required but external joseki engine missing, aborting.\n");
		if (DEBUGL(1)) fprintf(stderr, "Joseki fixes disabled: external joseki engine missing\n");
		disable_josekifix();
		return uct_engine;
	}

	/* Load josekifix database */
	if (!josekifix_init(b)) {
		delete_engine(&external_joseki_engine);
		disable_josekifix();
		return uct_engine;
	}
	
	engine_t *josekifix_engine = new_engine(E_JOSEKIFIX, NULL, b);
	return josekifix_engine;
}
