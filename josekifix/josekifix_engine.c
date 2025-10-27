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
#include "pachi.h"
#include "engines/external.h"
#include "josekifix/josekifix_engine.h"
#include "josekifix/joseki_override.h"
#include "tactics/util.h"
#include "dcnn/dcnn.h"
#include "uct/uct.h"


/* Engine state */
static engine_t  *uct_engine = NULL;
static engine_t  *external_joseki_engine = NULL;
static enum stone my_color = S_NONE;
static bool       undo_pending = false;
static bool       fake_external_joseki_engine = false;
static ownermap_t prev_ownermap;
static int        saved_external_joseki_engine_moves[4];

/* Globals */
char *external_joseki_engine_cmd = NULL;
char *katago_config = KATAGO_CONFIG;
char *katago_model  = KATAGO_MODEL;
bool  modern_joseki = false;
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

static char*
make_katago_command()
{
	/* Find katago config */
	char config[512];
	strbuf(build_tree_config, 512);
	strbuf_printf(build_tree_config, "josekifix/katago/%s", katago_config);
	get_data_file(config, katago_config);
	if (file_exists(config))
		;						/* In cwd, exe or data dir */
	else if (file_exists(build_tree_config->str))
		strcpy(config, build_tree_config->str);		/* Build tree */
	else {
		fprintf(stderr, "Loading katago config: %s\n", config);
		fprintf(stderr, "Katago config file missing, aborting.\n");
#ifdef _WIN32
		popup("ERROR: Couldn't find Katago config file.\n");
#endif
		exit(1);
	}

	/* Find model file */
	char model[512] = { 0, };
	strbuf(build_tree_model, 512);
	strbuf_printf(build_tree_model, "josekifix/katago/%s", katago_model);
	get_data_file(model, katago_model);
	if (file_exists(model))
		;						/* In cwd, exe or data dir */
	else if (file_exists(build_tree_model->str)) /* Build tree */
		strncpy(model, build_tree_model->str, 511);
	else {
		fprintf(stderr, "Loading katago model: %s\n", model);
		fprintf(stderr, "Katago model missing, aborting.\n");
#ifdef _WIN32
		popup("ERROR: Couldn't find Katago model.\n");
#endif
		exit(1);
	}

	/* Find Katago binary */
	char *binary = KATAGO_BINARY;
	bool has_path = (strchr(binary, '/') != NULL) || (strchr(binary, '\\') != NULL);
	strbuf(pachi_dir_binary, 512);
	sbprintf(pachi_dir_binary, "%s/%s", pachi_dir, KATAGO_BINARY);
	if (has_path)
		binary = KATAGO_BINARY;				/* Full path given */
	else if (file_exists("./" KATAGO_BINARY))
		binary = "./" KATAGO_BINARY;			/* Local file */
	else if (file_exists(pachi_dir_binary->str))
		binary = pachi_dir_binary->str;			/* Exe directory */
	else if (file_exists("katago/cpp/" KATAGO_BINARY))
		binary = "katago/cpp/" KATAGO_BINARY;		/* Build tree */
	// else assume it's in PATH.

	static_strbuf(buf, 1024);
	sbprintf(buf, "%s gtp -config %s -model %s", binary, config, model);
	
	return buf->str;
}

static bool
start_external_joseki_engine(board_t *b)
{
	assert(!external_joseki_engine);

	/* Use user-provided command if present. */
	char *cmd;
	if (external_joseki_engine_cmd)
		cmd = external_joseki_engine_cmd;
	else
		cmd = make_katago_command();

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
	static coord_t cached_c = pass;
	coord_t c = pass;

	/* Return cached coord if we get called twice somehow (2 external engine overrides match ?)
	 * We definitely don't want to genmove twice in a row. */
	if (external_joseki_engine_genmoved)
		return cached_c;
	
	external_joseki_engine_genmoved = true;

	if (fake_external_joseki_engine) {	/* Fake engine ? Return first move available. */
		c = cached_c = b->f[0];
		if (DEBUGL(2))  fprintf(stderr, "external joseki engine move: %s  (fake)\n", coord2sstr(c));
		return c;
	}
	
	enum stone color = board_to_play(b);
	c = cached_c = external_joseki_engine->genmove(external_joseki_engine, b, NULL, color, false);
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
	my_color = color;
	return genmove(b, ti, color, pass_all_alive, uct_engine->genmove);
}

static coord_t
josekifix_engine_genmove_analyze(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	my_color = color;
	return genmove(b, ti, color, pass_all_alive, uct_engine->genmove_analyze);
}


/**********************************************************************************************************/
/* UCT plumbing */

/* Reset uct engine, josekifix engine itself must not be reset. */
static void
josekifix_engine_reset(engine_t *e, board_t *b)
{
	engine_reset(uct_engine, b);
}

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
josekifix_engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color, best_moves_t *best)
{
	uct_engine->best_moves(uct_engine, b, ti, color, best);
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

		/* Restore external engine counters */
		for (int q = 0; q < 4; q++)
			b->external_joseki_engine_moves_left_by_quadrant[q] = saved_external_joseki_engine_moves[q];

		reset_uct_engine(b);
	}
	if (!strcmp(cmd, "undo")) {
		undo_pending = true;

		/* Save and rewind external engine counters, board will be cleared !
		 * Attempt to preserve external engine counters across undo. We don't have enough information
		 * to do a perfect job (for example if counter is 0 we can't know if it was 1 or 0 before that)
		 * but we can make it work while the sequence is active by looking at the last move. */
		for (int q = 0; q < 4; q++) {
			int moves = b->external_joseki_engine_moves_left_by_quadrant[q];
			move_history_t *h = b->move_history;
			if (h->moves >= 1) {
				move_t last = h->move[h->moves - 1];  // Can't use last_move(b) here
				coord_t last2_coord = (h->moves >= 2 ? h->move[h->moves - 2].coord : pass);
				if (moves && last.color == my_color && coord_quadrant(last2_coord) == q)
					moves++;
				if (moves > 15)	 /* XXX Assume we started at 15, which is mostly the case. */
					moves = 0;
			}
			saved_external_joseki_engine_moves[q] = moves;
			b->external_joseki_engine_moves_left_by_quadrant[q] = moves;
		}
	}

	/* Forward command to external engine. */
	external_joseki_engine->notify(external_joseki_engine, b, id, cmd, args, gtp);

	/* Modern joseki: Init external engine counter at game start. */
	if (modern_joseki) {
		/* Catch game start if no clear_board command was issued. */
		bool missed_init = (b->moves < 15 && !b->external_joseki_engine_moves_left_by_quadrant[0]);
		if (!strcmp(cmd, "clear_board") || !strcmp(cmd, "boardsize") || missed_init)
			for (int q = 0; q < 4; q++)
				b->external_joseki_engine_moves_left_by_quadrant[q] = 15;
	}
	
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

	e->reset = josekifix_engine_reset;
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
