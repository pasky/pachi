#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "board.h"
#include "pachi.h"
#include "debug.h"
#include "engine.h"
#include "fbook.h"
#include "gtp.h"
#include "mq.h"
#include "uct/uct.h"
#include "version.h"
#include "timeinfo.h"
#include "ownermap.h"
#include "gogui.h"
#include "t-predict/predict.h"
#include "josekifix/josekifix.h"
#include "t-unit/test.h"
#include "fifo.h"


/* Sleep 5 seconds after a game ends to give time to kill the program. */
#define GAME_OVER_SLEEP 5

/* Don't put standalone globals in gtp.c, some engines call gtp_parse()
 * internally and your global will likely get changed unintentionally.
 * Add some field in gtp_t instead and access it from whatever gtp_t
 * context is appropriate. */

void
gtp_init(gtp_t *gtp, board_t *b)
{
	memset(gtp, 0, sizeof(*gtp));
	b->move_history = &gtp->history;
}

void
gtp_done(gtp_t *gtp)
{
	if (gtp->custom_name)   free(gtp->custom_name);
	if (gtp->banner)	free(gtp->banner);
	memset(gtp, 0, sizeof(*gtp));
}

typedef enum parse_code (*gtp_func_t)(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);

typedef struct
{
	char *cmd;
	gtp_func_t f;
} gtp_command_t;

static gtp_command_t *commands;

/* Output gtp reply's initial char ('=', '?') */
static void
gtp_prefix(gtp_t *gtp, char prefix)
{
	if (gtp->replied || gtp->quiet)  return;
	gtp->replied = true;
	
	if (gtp->quiet)    return;
	if (gtp->id >= 0)  printf("%c%d ", prefix, gtp->id);
	else               printf("%c ", prefix);
}

/* Finish gtp reply, output final '\n' if needed. */
static void
gtp_flush(gtp_t *gtp)
{
	if (gtp->flushed || gtp->quiet)  return;
	if (!gtp->replied)  gtp_reply(gtp, "");
	gtp->flushed = true;
	
	putchar('\n');
	fflush(stdout);		/* in network mode stdout is not line-buffered */
}

/* Output one line, end-of-line \n added automatically. */
void
gtp_reply(gtp_t *gtp, const char *str)
{
	if (gtp->quiet)  return;
	
	gtp_prefix(gtp, '=');
	if (str)  fputs(str, stdout);
	putchar('\n');
}

void
gtp_error(gtp_t *gtp, const char *str)
{
	gtp->error = true;
	
	/* errors never quiet */
	gtp_prefix(gtp, '?');
	fputs(str, stdout);
	putchar('\n');
	gtp_flush(gtp);  /* flush errors right away */
}

/* Output anything (no \n added). */
void
gtp_printf(gtp_t *gtp, const char *format, ...)
{
	if (gtp->quiet)  return;

	va_list ap;
	va_start(ap, format);

	gtp_prefix(gtp, '=');
	vprintf(format, ap);

	va_end(ap);
}

void
gtp_error_printf(gtp_t *gtp, const char *format, ...)
{
	gtp->error = true;
	
	/* errors never quiet */
	va_list ap;
	va_start(ap, format);

	gtp_prefix(gtp, '?');
	vprintf(format, ap);
	gtp_flush(gtp);   /* flush errors right away */

	va_end(ap);	
}

/* Everything should be using gtp_reply() etc from here on. */
#define gtp_prefix  dont_call_gtp_prefix


/* List of public gtp commands. The internal command pachi-genmoves is not exported,
 * it should only be used between master and slaves of the distributed engine.
 * kgs-chat command enabled only if --kgs-chat passed (makes kgsgtp-3.5.20+ crash).
 * For now only uct engine supports gogui-analyze_commands. */
static char*
known_commands(gtp_t *gtp)
{
	static_strbuf(buf, 8192);

	for (int i = 0; commands[i].cmd; i++) {
		char *cmd = commands[i].cmd;
		if (str_prefix("pachi-genmoves", cmd))           continue;
		if (!strcmp("kgs-chat", cmd) && !gtp->kgs_chat)  continue;
		sbprintf(buf, "%s\n", commands[i].cmd);
	}
	
	sbprintf(buf, "gogui-analyze_commands\n");
	return buf->str;
}

static gtp_func_t
gtp_get_handler(const char *cmd)
{
	if (!cmd)  return NULL;
	
	for (int i = 0; commands[i].cmd; i++)
		if (!strcasecmp(cmd, commands[i].cmd))
			return commands[i].f;
	return NULL;
}

/* Return true if cmd is a valid gtp command. */
bool
gtp_is_valid(engine_t *e, const char *cmd)
{
	if (!cmd || !*cmd)  return false;
	return (gtp_get_handler(cmd) != NULL);
}

static enum parse_code
cmd_protocol_version(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_reply(gtp, "2");
	return P_OK;
}

static enum parse_code
cmd_name(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *name = "Pachi %s";
	if (!strcmp(e->name, "UCT"))  name = "Pachi";
	if (gtp->custom_name)         name = gtp->custom_name;
	gtp_printf(gtp, name, e->name);
	gtp_printf(gtp, "\n");
	return P_OK;
}

static enum parse_code
cmd_echo(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "%s", gtp->next);
	return P_OK;
}

/* Return Pachi version.
 * On kgs also return banner (game start message, set with --banner). */
static enum parse_code
cmd_version(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "%s", PACHI_VERSION);

	/* Show josekifix status */
 	if (!get_josekifix_enabled() && e->id == E_UCT)
		gtp_printf(gtp, " (joseki fixes disabled)");
	
	/* kgs hijacks 'version' gtp command for game start message. */
	if (gtp->kgs && gtp->banner)
		gtp_printf(gtp, ". %s", gtp->banner);
	
	gtp_printf(gtp, "\n");
	return P_OK;
}

static enum parse_code
cmd_list_commands(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "%s", known_commands(gtp));
	return P_OK;
}

static enum parse_code
cmd_known_command(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	if (gtp_is_valid(e, arg))  gtp_reply(gtp, "true");
	else                       gtp_reply(gtp, "false");
	return P_OK;
}

static enum parse_code
cmd_quit(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_flush(gtp);
	pachi_done();
	exit(0);
}

static enum parse_code
cmd_boardsize(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	int size = atoi(arg);

	/* Give sane error msg if pachi was compiled for a specific board size. */
#ifdef BOARD_SIZE
	if (size != BOARD_SIZE) {
		gtp_error_printf(gtp, "This Pachi only plays on %ix%i.\n", BOARD_SIZE, BOARD_SIZE);
		die("Yozaa ! This Pachi only plays on %ix%i.\n", BOARD_SIZE, BOARD_SIZE);
	}
#endif
		    
	if (size < 1 || size > BOARD_MAX_SIZE) {
		gtp_error(gtp, "illegal board size");
		return P_OK;
	}
	board_resize(b, size);
	board_clear(b);
	return P_ENGINE_RESET;
}

static enum parse_code
cmd_clear_board(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	board_clear(b);
	gtp->played_games++;
	if (DEBUGL(3) && debug_boardprint)
		board_print(b, stderr);

	return P_ENGINE_RESET;
}

static enum parse_code
cmd_kgs_game_over(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* The game may not be really over, just adjourned.
	 * Do not clear the board to avoid illegal moves
	 * if the game is resumed immediately after. KGS
	 * may start directly with genmove on resumption. */
	if (DEBUGL(1)) {
		fprintf(stderr, "game is over\n");
		fflush(stderr);
	}
	if (e->stop)
		e->stop(e);
	/* Sleep before replying, so that kgs doesn't
	 * start another game immediately. */
	sleep(GAME_OVER_SLEEP);
	return P_OK;
}

static enum parse_code
cmd_komi(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	sscanf(arg, PRIfloating, &b->komi);

	if (DEBUGL(3) && debug_boardprint)
		board_print(b, stderr);
	return P_OK;
}

static enum parse_code
cmd_kgs_rules(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);

	/* Print timestamp at game start, makes logs more useful */
	if (DEBUGL(2))  fprintf(stderr, "%s\n", time_str());
	
	if (pachi_options()->forced_rules) {
		if (DEBUGL(2))  fprintf(stderr, "ignored kgs-rules, using %s.\n", rules2str(b->rules));
		return P_OK;
	}
	
	if (!board_set_rules(b, arg))
		gtp_error(gtp, "unknown rules");

	return P_OK;
}

static enum parse_code
cmd_play(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	move_t m;

	char *arg;
	gtp_arg(arg);
	m.color = str2stone(arg);
	gtp_arg(arg);
	m.coord = str2coord(arg);
	arg = gtp->next;
	char *enginearg = arg;

	// This is where kgs starts the timer, not at genmove!
	time_start_timer(&ti[stone_other(m.color)]);

	// XXX engine getting notified if move is illegal !
	bool print = false;
	char *reply = (e->notify_play ? e->notify_play(e, b, &m, enginearg, &print) : NULL);
	
	if (board_play(b, &m) < 0) {
		if (DEBUGL(0)) {
			fprintf(stderr, "! ILLEGAL MOVE %s %s\n", stone2str(m.color), coord2sstr(m.coord));
			board_print(b, stderr);
		}
		gtp_error(gtp, "illegal move");
		return P_OK;
	}

	if (print || (DEBUGL(4) && debug_boardprint))
		engine_board_print(e, b, stderr);
	
	gtp_reply(gtp, reply);
	return P_OK;
}

static enum parse_code
cmd_pachi_predict(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	move_t m;
	char *arg;
	gtp_arg(arg);
	m.color = str2stone(arg);
	gtp_arg(arg);
	m.coord = str2coord(arg);

	char *str = predict_move(b, e, ti, &m, gtp->played_games);

	gtp_reply(gtp, str);
	free(str);
	return P_OK;
}

/* Get move from engine, or joseki override if there is one.
 * There are 2 joseki override hooks : one before engine genmove (this one),
 * and another one at the end of uct genmove. Without external engine we'd
 * need only the second one, but with 2 engines we want to avoid asking both
 * engines as that would mean a serious delay. So this acts as a dispatch,
 * short-cirtuiting engine genmove when we know it will be overridden by an
 * external engine move. */
static coord_t
genmove_get_move(board_t *b, enum stone color, engine_t *e, time_info_t *ti_genmove,
		 gtp_t *gtp, engine_genmove_t genmove_func)
{
	bool pass_all_alive = !strcasecmp(gtp->cmd, "kgs-genmove_cleanup");    
	coord_t c = pass;

#ifdef JOSEKIFIX
	if (is_pass(c) && e->id == E_UCT)
		c = joseki_override_before_genmove(b, color);
#endif
	
	if (is_pass(c))
		c = genmove_func(e, b, ti_genmove, color, pass_all_alive);

	return c;
}

static coord_t
genmove(board_t *b, enum stone color, engine_t *e, time_info_t *ti, gtp_t *gtp, engine_genmove_t genmove_func)
{
	if (DEBUGL(2) && debug_boardprint)
		engine_board_print(e, b, stderr);
		
	if (!ti[color].timer_start)    /* First game move. */
		time_start_timer(&ti[color]);
	
#ifdef PACHI_FIFO   /* Coordinate between multiple Pachi instances. */
	double time_wait = time_now();
	int ticket = fifo_task_queue();
	double time_start = time_now();
#endif

#ifdef JOSEKIFIX
	external_joseki_engine_genmoved = 0;
#endif
	
	time_info_t *ti_genmove = time_info_genmove(b, ti, color);
	coord_t c = (b->fbook ? fbook_check(b) : pass);
	if (is_pass(c))
		c = genmove_get_move(b, color, e, ti_genmove, gtp, genmove_func);

#ifdef PACHI_FIFO
	if (DEBUGL(2)) fprintf(stderr, "fifo: genmove in %0.2fs  (waited %0.1fs)\n", time_now() - time_start, time_start - time_wait);
	fifo_task_done(ticket);
#endif

	if (!is_resign(c)) {
		move_t m = move(c, color);
		if (board_play(b, &m) < 0)
			die("Attempted to generate an illegal move: %s %s\n", stone2str(m.color), coord2sstr(m.coord));

#ifdef JOSEKIFIX
		/* send new move to external engine if it doesn't come from it */
		if (!external_joseki_engine_genmoved && e->id == E_UCT)
			external_joseki_engine_play(c, color);
#endif
	}
	
	char *str = coord2sstr(c);
	if (DEBUGL(4))                      fprintf(stderr, "playing move %s\n", str);
	if (DEBUGL(1) && debug_boardprint)  engine_board_print(e, b, stderr);
	
	/* Account for spent time. If our GTP peer keeps our clock, this will
	 * be overriden by next time_left GTP command properly. */
	/* (XXX: Except if we pass to byoyomi and the peer doesn't, but that
	 * should be absolutely rare situation and we will just spend a little
	 * less time than we could on next few moves.) */
	if (ti[color].type != TT_NULL && ti[color].dim == TD_WALLTIME)
		time_sub(&ti[color], time_now() - ti[color].timer_start, true);
	
	return c;
}

static enum parse_code
cmd_genmove(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);

	coord_t c = genmove(b, color, e, ti, gtp, e->genmove);
	gtp_reply(gtp, coord2sstr(c));
	return P_OK;
}

/* lz-genmove_analyze: get winrates etc during genmove.
 * Similar to Leela-zero lz-genmove_analyze 
 * syntax: lz-genmove_analyze <color> <freq>   */
static enum parse_code
cmd_lz_genmove_analyze(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);
	if (color == S_NONE) {  gtp_error(gtp, "bad argument"); return P_OK;  }
	gtp_arg(arg);
	if (!isdigit(*arg))  {  gtp_error(gtp, "bad argument"); return P_OK;  }
	int freq = atoi(arg);  /* frequency (centiseconds) */

	if (!e->genmove_analyze) {  gtp_error(gtp, "lz-genmove_analyze not supported for this engine"); return P_OK; }

	strbuf(buf, 100);  char *err;
	sbprintf(buf, "reportfreq=%fs", 0.01 * freq);
	bool r = engine_setoptions(e, b, buf->str, &err);  assert(r);

	gtp_printf(gtp, "\n");
	coord_t c = genmove(b, color, e, ti, gtp, e->genmove_analyze);
	printf("play %s\n", coord2sstr(c));
	return P_OK;
}

/* Used by slaves in distributed mode.
 * Special: may send binary data after gtp reply. */
static enum parse_code
cmd_pachi_genmoves(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);
	void *stats;
	int stats_size;

	time_info_t *ti_genmove = time_info_genmove(b, ti, color);
	char *reply = e->genmoves(e, b, ti_genmove, color, gtp->next,
				  !strcasecmp(gtp->cmd, "pachi-genmoves_cleanup"),
				  &stats, &stats_size);
	if (!reply) {  gtp_error(gtp, "genmoves error");  return P_OK;	}
	if (DEBUGL(3))                      fprintf(stderr, "proposing moves %s\n", reply);
	if (DEBUGL(4) && debug_boardprint)  engine_board_print(e, b, stderr);
	
	gtp_reply(gtp, reply);
	putchar('\n');		// gtp_flush() sortof,
	gtp->flushed = true;	// but we handle fflush() ourselves here.

	if (stats_size > 0) {   // send binary part
		double start = time_now();
		fwrite(stats, 1, stats_size, stdout);
		if (DEBUGVV(3))
			fprintf(stderr, "sent reply %d bytes in %.4fms\n",
				stats_size, (time_now() - start)*1000);
	}
	fflush(stdout);
	return P_OK;
}

static void
gtp_reset_engine(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti)
{
	engine_reset(e, b);

	/* Reset timer */
	ti[S_BLACK].timer_start = 0;
	ti[S_WHITE].timer_start = 0;
}

static int
engine_pondering(engine_t *e)
{
	option_t *o = engine_options_lookup(&e->options, "pondering");
	if (!o)  return 0;
	return (!o->val || atoi(o->val));
}

/* Keep track of analyze mode / genmove mode and manage engine.
 * Allows to reset engine only when needed so we don't lose analyze data
 * when toggling analyze on and off.
 * normal:    reset engine when switching from analyze mode -> genmove mode
 *            analyze tree shouldn't affect next genmove.
 * pondering: don't reset !
 *            engine handles switching from pondering <-> pondering + analyzing */
static void
gtp_set_analyze_mode(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, bool analyze_mode)
{
	if (analyze_mode != gtp->analyze_mode) {
		// fprintf(stderr, "gtp: switching analyze_mode: %i -> %i\n", gtp->analyze_mode, analyze_mode);
		gtp->analyze_mode = analyze_mode;

		if (!engine_pondering(e))
			if (!analyze_mode)  /* analyze mode -> genmove mode */
				gtp_reset_engine(gtp, b, e, ti);
	}
}

static void
stop_analyzing(gtp_t *gtp, board_t *b, engine_t *e)
{
	gtp->analyze_running = false;
	e->analyze(e, b, S_BLACK, 0);
	printf("\n");  /* end of lz-analyze output */
	fflush(stdout);
}

/* Start pondering and output stats for the sake of frontend running Pachi.
 * Stop processing when we receive some other command.
 * Similar to Leela-Zero's lz-analyze so we can feed data to Lizzie / Sabaki.
 * Usage: lz-analyze <freq>		(centiseconds)
 *        lz-analyze <color> <freq>
 * lz-analyze with allow move / avoid move syntax unsupported right now. */
static enum parse_code
cmd_lz_analyze(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone color = board_to_play(b);
	char *arg;
	gtp_arg(arg);
	
	/* optional color argument ? */
	if (tolower(arg[0]) == 'w' || tolower(arg[0]) == 'b') {  color = str2stone(arg);  gtp_arg(arg);  }
	if (!isdigit(*arg)) {  gtp_error(gtp, "bad argument"); return P_OK;  }
	if (!e->analyze)    {  gtp_error(gtp, "lz-analyze not supported for this engine"); return P_OK;  }
	int freq = atoi(arg);  /* frequency (centiseconds) */

	if (!freq)  {  stop_analyzing(gtp, b, e);  return P_OK;  }

	strbuf(buf, 100); char *err;
	sbprintf(buf, "reportfreq=%fs", 0.01 * freq);
	bool r = engine_setoptions(e, b, buf->str, &err);  assert(r);

	gtp_printf(gtp, "");   /* just "= \n" output, last newline will be sent when we stop analyzing */
	gtp_set_analyze_mode(gtp, b, e, ti, true);
	gtp->analyze_running = true;
	e->analyze(e, b, color, 1);
	
	return P_OK;
}

static enum parse_code
cmd_set_free_handicap(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	do {
		move_t m = move(str2coord(arg), S_BLACK);
		if (DEBUGL(4))  fprintf(stderr, "setting handicap %s\n", arg);

		// XXX board left in inconsistent state if illegal move comes in
		if (board_play(b, &m) < 0) {
			if (DEBUGL(0))  fprintf(stderr, "! ILLEGAL MOVE %s\n", arg);
			gtp_error(gtp, "illegal move");
		}
		
		b->handicap++;
		gtp_arg_optional(arg);
	} while (*arg);
	
	if (DEBUGL(1) && debug_boardprint)
		board_print(b, stderr);
	return P_OK;
}

/* TODO: Engine should choose free handicap; however, it tends to take
 * overly long to think it all out, and unless it's clever its
 * handicap stones won't be of much help. ;-) */
static enum parse_code
cmd_fixed_handicap(board_t *b, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	int stones = atoi(arg);
	
	move_queue_t q;  mq_init(&q);
	board_handicap(b, stones, &q);
	
	if (DEBUGL(1) && debug_boardprint)
		board_print(b, stderr);

	for (int i = 0; i < q.moves; i++) {
		move_t m = move(q.move[i], S_BLACK);
		gtp_printf(gtp, "%s ", coord2sstr(m.coord));
	}
	gtp_printf(gtp, "\n");

#ifdef JOSEKIFIX
	if (external_joseki_engine && !strcmp(gtp->cmd, "place_free_handicap") && engine->id == E_UCT)
		external_joseki_engine_fixed_handicap(stones);			// XXX assumes other engine places fixed handi stones like us ...
#endif
	
	return P_OK;
}

static enum parse_code
cmd_final_score(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *msg = NULL;
	ownermap_t *o = engine_ownermap(e, b);
	if (o && !board_position_final(b, o, &msg)) {
		gtp_error(gtp, msg);
		return P_OK;
	}

	move_queue_t q;
	engine_dead_groups(e, b, &q);
	char *score_str = board_official_score_str(b, &q);

	if (DEBUGL(1))  fprintf(stderr, "official score: %s\n", score_str);
	gtp_printf(gtp, "%s\n", score_str);

	return P_OK;
}

static enum parse_code
cmd_pachi_score_est(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap)  {  gtp_error(gtp, "no ownermap");  return P_OK;  }

	board_print_ownermap(b, stderr, ownermap);
	gtp_reply(gtp, ownermap_score_est_str(b, ownermap));
	return P_OK;
}

static enum parse_code
cmd_pachi_setoption(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg, *err;
	gtp_arg(arg);
	if (!engine_setoptions(e, b, arg, &err))
	    gtp_error(gtp, err);
	return P_OK;
}

/* Get engine option(s):
 * Without arg, return all options (comma-separated)
 * With arg (option name), return option value. */
static enum parse_code
cmd_pachi_getoption(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *name;
	gtp_arg_optional(name);

	if (*name) {  /* Return option value */
		option_t *o = engine_options_lookup(&e->options, name);
		if (!o)  gtp_error(gtp, "option not set");
		else     gtp_reply(gtp, (o->val ? o->val : ""));
		return P_OK;
	}

	/* Dump all options. */
	strbuf(buf, 1024);
	engine_options_concat(buf, &e->options);
	gtp_reply(gtp, buf->str);
	return P_OK;
}

static int
cmd_final_status_list_dead(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	move_queue_t q;
	engine_dead_groups(e, b, &q);

	for (int i = 0; i < q.moves; i++) {
		foreach_in_group(b, q.move[i]) {
			gtp_printf(gtp, "%s ", coord2sstr(c));
		} foreach_in_group_end;
		gtp_printf(gtp, "\n");
	}

	if (DEBUGL(1)) {   /* show final score and board */
		fprintf(stderr, "\nfinal score: %s  (%s)\n", board_official_score_str(b, &q), rules2str(b->rules));
		board_print_official_ownermap(b, &q);
	}

	return q.moves;
}

static int
cmd_final_status_list_alive(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	move_queue_t q;
	engine_dead_groups(e, b, &q);
	int printed = 0;
	
	foreach_point(b) { // foreach_group, effectively
		group_t g = group_at(b, c);
		if (!g || g != c) continue;

		for (int i = 0; i < q.moves; i++)
			if (q.move[i] == g)  goto next_group;
			
		foreach_in_group(b, g) {
			gtp_printf(gtp, "%s ", coord2sstr(c));
		} foreach_in_group_end;
		gtp_printf(gtp, "\n");  printed++;
	next_group:;
	} foreach_point_end;
	return printed;
}

static int
cmd_final_status_list_seki(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap) {  gtp_error(gtp, "no ownermap");  return -1;  }
	int printed = 0;

	move_queue_t sekis;  mq_init(&sekis);
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		if (ownermap_judge_point(ownermap, c, 0.80) != PJ_SEKI)  continue;

		foreach_neighbor(b, c, {
			group_t g = group_at(b, c);
			if (!g)  continue;
			mq_add(&sekis, g, 0);
			mq_nodup(&sekis);
		});
	} foreach_point_end;

	for (int i = 0; i < sekis.moves; i++) {
		foreach_in_group(b, sekis.move[i]) {
			gtp_printf(gtp, "%s ", coord2sstr(c));
		} foreach_in_group_end;
		gtp_printf(gtp, "\n");  printed++;
	}

	return printed;
}

static int
cmd_final_status_list_territory(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	enum stone color = str2stone(arg);
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap) {  gtp_error(gtp, "no ownermap");  return -1;  }
		
	foreach_point(b) {
		if (board_at(b, c) != S_NONE)  continue;
		if (ownermap_color(ownermap, c, 0.67) != color)  continue;
		gtp_printf(gtp, "%s ", coord2sstr(c));
	} foreach_point_end;
	gtp_printf(gtp, "\n");
	return 1;
}	

/* XXX: This is a huge hack. */
static enum parse_code
cmd_final_status_list(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (gtp->quiet)  return P_OK;
	char *arg;
	gtp_arg(arg);
	int r = -1;
	
	if      (!strcasecmp(arg, "dead"))            r = cmd_final_status_list_dead(arg, b, e, gtp);
	else if (!strcasecmp(arg, "alive"))           r = cmd_final_status_list_alive(arg, b, e, gtp);
	else if (!strcasecmp(arg, "seki"))            r = cmd_final_status_list_seki(arg, b, e, gtp);
	else if (!strcasecmp(arg, "black_territory")  ||       /* gnugo extensions */
		 !strcasecmp(arg, "white_territory")) r = cmd_final_status_list_territory(arg, b, e, gtp);
	else    gtp_error(gtp, "illegal status specifier");

	if (r < 0)  return P_OK;
	if (!r)  gtp_printf(gtp, "\n");
	return P_OK;
}

/* Handle undo at the gtp level. */
static enum parse_code
cmd_undo(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* --noundo: undo only allowed for pass. */
	if (gtp->noundo && !is_pass(last_move(b).coord)) {
		if (DEBUGL(1))  fprintf(stderr, "undo on non-pass move %s\n", coord2sstr(last_move(b).coord));
		gtp_error(gtp, "cannot undo");
		return P_OK;
	}

	move_history_t *h = &gtp->history;
	if (!h->moves) {  gtp_error(gtp, "no moves to undo");  return P_OK;  }
	if (b->moves == b->handicap) {  gtp_error(gtp, "can't undo handicap");  return P_OK;  }
	h->moves--;
	
	/* Send a play command to engine so it stops pondering (if it was pondering).  */
	move_t m = move(pass, board_to_play(b));  bool print;
	if (e->notify_play)
		e->notify_play(e, b, &m, "", &print);

	/* Wait for non-undo command to reset engine. */
	gtp->undo_pending = true;
	
	return P_OK;
}

static void
undo_reload_engine(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti)
{
	if (DEBUGL(3)) fprintf(stderr, "reloading engine after undo(s).\n");
	
	gtp->undo_pending = false;

	gtp_reset_engine(gtp, b, e, ti);
	
	/* Reset board */
	int handicap = b->handicap;
	b->move_history = NULL;		/* Preserve history ! */
	board_clear(b);
	b->handicap = handicap;

	move_history_t *h = &gtp->history;
	for (int i = 0; i < h->moves; i++) {
		bool print;
		if (e->notify_play)
			e->notify_play(e, b, &h->move[i], "", &print);
		int r = board_play(b, &h->move[i]);
		assert(r >= 0);
	}

	b->move_history = &gtp->history;
}

static enum parse_code
cmd_showboard(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "");
	board_print(b, stdout);
	gtp->flushed = 1;  // already ends with \n\n
	fflush(stdout);
	return P_OK;
}

/* Custom commands for handling the tree opening tbook */
static enum parse_code
cmd_pachi_gentbook(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* Board must be initialized properly, as if for genmove;
	 * makes sense only as 'uct_gentbook b'. */
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);
	if (!uct_gentbook(e, b, &ti[color], color))
		gtp_error(gtp, "error generating tbook");
	return P_OK;
}

static enum parse_code
cmd_pachi_dumptbook(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);
	uct_dumptbook(e, b, color);
	return P_OK;
}

static enum parse_code
cmd_pachi_evaluate(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);

	if (!e->evaluate) {
		gtp_error(gtp, "pachi-evaluate not supported by engine");
	} else {
		floating_t vals[b->flen];
		e->evaluate(e, b, &ti[color], vals, color);
		for (int i = 0; i < b->flen; i++) {
			if (isnan(vals[i]) || vals[i] < 0.001)
				continue;
			gtp_printf(gtp, "%s %.3f\n", coord2sstr(b->f[i]), (double) vals[i]);
		}
	}
	return P_OK;
}

static enum parse_code
cmd_pachi_result(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* More detailed result of the last genmove. */
	/* For UCT, the output format is: = color move playouts winrate dynkomi */
	char *reply = (e->result ? e->result(e, b) : NULL);
	if (reply)  gtp_reply(gtp, reply);
	else        gtp_error(gtp, "unknown pachi-result command");
	return P_OK;
}

static enum parse_code
cmd_pachi_tunit(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	int res = unit_test_cmd(b, gtp->next);
	const char *str = (res ? "passed" : "failed");
	gtp_reply(gtp, str);
	return P_OK;
}

static enum parse_code
cmd_kgs_chat(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *loc;
	gtp_arg(loc);
	bool opponent = !strcasecmp(loc, "game");
	char *from;
	gtp_arg(from);
	char *msg = gtp->next;
	msg += strspn(msg, " \n\t");
	char *end = strchr(msg, '\n');
	if (end) *end = '\0';
	char *reply = (e->chat ? e->chat(e, b, opponent, from, msg) : NULL);
	if (reply)  gtp_reply(gtp, reply);
	else        gtp_error(gtp, "unknown kgs-chat command");
	return P_OK;
}

static enum parse_code
cmd_time_left(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);
	enum stone color = str2stone(arg);
	gtp_arg(arg);
	int time = atoi(arg);
	gtp_arg(arg);
	int stones = atoi(arg);
	if (!ti[color].ignore_gtp)
		time_left(&ti[color], time, stones);
	else
		if (DEBUGL(2)) fprintf(stderr, "ignored time info\n");
	return P_OK;
}

static enum parse_code
cmd_kgs_time_settings(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *time_system;
	char *arg;
	if (!strcasecmp(gtp->cmd, "kgs-time_settings")) {
		gtp_arg(time_system);
	} else {
		time_system = "canadian";
	}

	int main_time = 0, byoyomi_time = 0, byoyomi_stones = 0, byoyomi_periods = 0;
	if (!strcasecmp(time_system, "none")) {
		main_time = -1;
	} else if (!strcasecmp(time_system, "absolute")) {
		gtp_arg(arg);
		main_time = atoi(arg);
	} else if (!strcasecmp(time_system, "byoyomi")) {
		gtp_arg(arg);
		main_time = atoi(arg);
		gtp_arg(arg);
		byoyomi_time = atoi(arg);
		gtp_arg(arg);
		byoyomi_periods = atoi(arg);
	} else if (!strcasecmp(time_system, "canadian")) {
		gtp_arg(arg);
		main_time = atoi(arg);
		gtp_arg(arg);
		byoyomi_time = atoi(arg);
		gtp_arg(arg);
		byoyomi_stones = atoi(arg);
	}

	if (DEBUGL(1))
		fprintf(stderr, "time_settings %d %d/%d*%d\n",
			main_time, byoyomi_time, byoyomi_stones, byoyomi_periods);
	if (!ti[S_BLACK].ignore_gtp) {
		time_settings(&ti[S_BLACK], main_time, byoyomi_time, byoyomi_stones, byoyomi_periods);
		ti[S_WHITE] = ti[S_BLACK];
	} else {
		if (DEBUGL(1)) fprintf(stderr, "ignored time info\n");
	}

	return P_OK;
}


static gtp_command_t gtp_commands[] =
{
	{ "protocol_version",       cmd_protocol_version },
	{ "name",                   cmd_name },
	{ "echo",                   cmd_echo },
	{ "version",                cmd_version },
	{ "list_commands",          cmd_list_commands },
	{ "known_command",          cmd_known_command },
	{ "quit",                   cmd_quit },
	{ "boardsize",              cmd_boardsize },
	{ "clear_board",            cmd_clear_board },
	{ "komi",                   cmd_komi },
	{ "play",                   cmd_play },
	{ "genmove",                cmd_genmove },
	{ "time_left",              cmd_time_left },
	{ "time_settings",          cmd_kgs_time_settings },
	{ "set_free_handicap",      cmd_set_free_handicap },
	{ "place_free_handicap",    cmd_fixed_handicap },
	{ "fixed_handicap",         cmd_fixed_handicap },
	{ "final_score",            cmd_final_score },
	{ "final_status_list",      cmd_final_status_list },
	{ "undo",                   cmd_undo },
	{ "showboard",              cmd_showboard },   	/* ogs */

	{ "kgs-game_over",          cmd_kgs_game_over },
	{ "kgs-rules",              cmd_kgs_rules },
	{ "kgs-genmove_cleanup",    cmd_genmove },
	{ "kgs-time_settings",      cmd_kgs_time_settings },
	{ "kgs-chat",               cmd_kgs_chat },

	{ "pachi-predict",          cmd_pachi_predict },
	{ "pachi-tunit",            cmd_pachi_tunit },
	{ "pachi-genmoves",         cmd_pachi_genmoves },
	{ "pachi-genmoves_cleanup", cmd_pachi_genmoves },
	{ "pachi-gentbook",         cmd_pachi_gentbook },
	{ "pachi-dumptbook",        cmd_pachi_dumptbook },
	{ "pachi-evaluate",         cmd_pachi_evaluate },
	{ "pachi-result",           cmd_pachi_result },
	{ "pachi-score_est",        cmd_pachi_score_est },
	{ "pachi-setoption",	    cmd_pachi_setoption },  /* Set/change engine option */
	{ "pachi-getoption",	    cmd_pachi_getoption },  /* Get engine option(s) */

	{ "lz-analyze",             cmd_lz_analyze },         /* Lizzie, Sabaki, etc */
	{ "lz-genmove_analyze",     cmd_lz_genmove_analyze },

	/* Short aliases */
	{ "predict",                cmd_pachi_predict },
	{ "tunit",		    cmd_pachi_tunit },
	{ "score_est",              cmd_pachi_score_est },

	{ "gogui-analyze_commands", cmd_gogui_analyze_commands },
	{ "gogui-livegfx",          cmd_gogui_livegfx },
	{ "gogui-influence",        cmd_gogui_influence },
	{ "gogui-score_est",        cmd_gogui_score_est },
	{ "gogui-final_score",      cmd_gogui_final_score },
	{ "gogui-best_moves",       cmd_gogui_best_moves },
	{ "gogui-winrates",         cmd_gogui_winrates },
	{ "gogui-joseki_moves",     cmd_gogui_joseki_moves },
	{ "gogui-joseki_show_pattern", cmd_gogui_joseki_show_pattern },
#ifdef DCNN
	{ "gogui-dcnn_best",        cmd_gogui_dcnn_best },
	{ "gogui-dcnn_colors",      cmd_gogui_dcnn_colors },
	{ "gogui-dcnn_rating",      cmd_gogui_dcnn_rating },
#endif /* DCNN */
	{ "gogui-pattern_best",     cmd_gogui_pattern_best },
	{ "gogui-pattern_colors",   cmd_gogui_pattern_colors },
	{ "gogui-pattern_rating",   cmd_gogui_pattern_rating },
	{ "gogui-pattern_features", cmd_gogui_pattern_features },
	{ "gogui-pattern_gammas",   cmd_gogui_pattern_gammas },
	{ "gogui-show_spatial",     cmd_gogui_show_spatial },
	{ "gogui-spatial_size",     cmd_gogui_spatial_size },
	{ "gogui-color_palette",    cmd_gogui_color_palette },
#ifdef JOSEKIFIX
	{ "gogui-josekifix_set_coord",    cmd_gogui_josekifix_set_coord },
	{ "gogui-josekifix_show_pattern", cmd_gogui_josekifix_show_pattern },
	{ "gogui-josekifix_dump_templates", cmd_gogui_josekifix_dump_templates },
#endif

	{ 0, 0 }
};

void
gtp_internal_init(gtp_t *gtp)
{
	commands = gtp_commands;  /* c++ madness */
}

/* XXX: THIS IS TOTALLY INSECURE!!!!
 * Even basic input checking is missing. */

static enum parse_code
gtp_run_handler(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, char *buf)
{
	gtp_func_t handler = gtp_get_handler(gtp->cmd);

	if (!handler) {
		gtp_error(gtp, "unknown command");
		return P_UNKNOWN_COMMAND;
	}
	
	/* Run engine notify() handler */
	if (e->notify) {
		enum parse_code c = e->notify(e, b, gtp->id, gtp->cmd, gtp->next, gtp);
		
		if (gtp->error)  return P_OK;		      /* error, don't run default handler */

		if (gtp->replied && c == P_OK)
			die("gtp: %s engine's notify() silently overrides default handler for cmd '%s', that's bad\n", e->name, gtp->cmd);
		
		if      (c == P_NOREPLY)  gtp->quiet = true;  /* run default handler but suppress output */
		else if (c == P_DONE_OK)  return P_OK;	      /* override, don't run default handler */
		else if (c != P_OK)       return c;	      /* (right now P_ENGINE_RESET would override default handler) */
	}

	/* Run default handler */
	return handler(b, e, ti, gtp);
}

enum parse_code
gtp_parse(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, char *buf)
{
	if (strchr(buf, '#'))
		*strchr(buf, '#') = 0;

	char orig_cmd[1024];
	strncpy(orig_cmd, buf, sizeof(orig_cmd) - 1);
	
	/* Reset non global fields. */
	gtp->id = -1;
	gtp->next = buf;
	gtp->replied = false;
	gtp->flushed = false;
	gtp->error = false;
	gtp_arg_optional(gtp->cmd);
	
	if (isdigit(*gtp->cmd)) {
		gtp->id = atoi(gtp->cmd);
		gtp_arg(gtp->cmd);
	}

	if (!*gtp->cmd)
		return P_OK;

	if (gtp->analyze_running && strcasecmp(gtp->cmd, "lz-analyze"))
		stop_analyzing(gtp, b, e);
	if (gtp->analyze_mode && strstr(gtp->cmd, "genmove"))
		gtp_set_analyze_mode(gtp, b, e, ti, false);

#ifdef JOSEKIFIX
	if (e->id == E_UCT)
		external_joseki_engine_forward_cmd(gtp, orig_cmd);
#endif
	
	/* Undo: reload engine after first non-undo command. */
	if (gtp->undo_pending && strcasecmp(gtp->cmd, "undo"))
		undo_reload_engine(gtp, b, e, ti);

	/* Run handler */
	enum parse_code c = gtp_run_handler(gtp, b, e, ti, buf);
	assert(c == P_OK || c == P_ENGINE_RESET || c == P_UNKNOWN_COMMAND);

	/* Add final '\n' and empty reply if needed */
	if (!gtp->flushed)  gtp_flush(gtp);
	return c;
}
