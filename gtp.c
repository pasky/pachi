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
#include "t-unit/test.h"
#include "fifo.h"

/* Sleep 5 seconds after a game ends to give time to kill the program. */
#define GAME_OVER_SLEEP 5

/* Don't put standalone globals in gtp.c, some engines call gtp_parse()
 * internally and your global will likely get changed unintentionally.
 * Add some field in gtp_t instead and access it from whatever gtp_t
 * context is appropriate. */

void
gtp_init(gtp_t *gtp)
{
	memset(gtp, 0, sizeof(*gtp));
}


typedef enum parse_code (*gtp_func_t)(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);

typedef struct
{
	char *cmd;
	gtp_func_t f;
} gtp_command_t;

static gtp_command_t *commands;

void
gtp_prefix(char prefix, gtp_t *gtp)
{
	if (gtp->replied)  return;
	gtp->replied = true;
	
	if (gtp->quiet)    return;
	if (gtp->id >= 0)  printf("%c%d ", prefix, gtp->id);
	else               printf("%c ", prefix);
}

void
gtp_flush(void)
{
	putchar('\n');
	fflush(stdout);
}

void
gtp_output(char prefix, gtp_t *gtp, va_list params)
{
	if (gtp->quiet)  return;
	gtp_prefix(prefix, gtp);
	char *s;
	while ((s = va_arg(params, char *))) {
		fputs(s, stdout);
	}
	putchar('\n');
	gtp_flush();
}

void
gtp_reply(gtp_t *gtp, ...)
{
	va_list params;
	va_start(params, gtp);
	gtp_output('=', gtp, params);
	va_end(params);
}

/* Like gtp_reply() takes care of final \n\n so format must not have it. */
void
gtp_reply_printf(gtp_t *gtp, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	gtp_prefix('=', gtp);
	vprintf(format, ap);
	putchar('\n');
	gtp_flush();
	va_end(ap);	
}

void
gtp_error_printf(gtp_t *gtp, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	gtp_prefix('?', gtp);
	vprintf(format, ap);
	gtp_flush();
	va_end(ap);	
}

void
gtp_error(gtp_t *gtp, ...)
{
	va_list params;
	va_start(params, gtp);
	gtp_output('?', gtp, params);
	va_end(params);
}

/* List of public gtp commands. The internal command pachi-genmoves is not exported,
 * it should only be used between master and slaves of the distributed engine.
 * For now only uct engine supports gogui-analyze_commands. */
static char*
known_commands(engine_t *engine)
{
	static char buf[8192];
	char *str = buf;
	
	for (int i = 0; commands[i].cmd; i++) {
		char *cmd = commands[i].cmd;
		if (str_prefix("pachi-genmoves", cmd))
			continue;		
		str += sprintf(str, "%s\n", commands[i].cmd);
	}
	
	str += sprintf(str, "gogui-analyze_commands\n");
	str[-1] = 0;  /* remove last \n */
	return buf;
}

/* Return true if cmd is a valid gtp command. */
bool
gtp_is_valid(engine_t *e, const char *cmd)
{
	if (!cmd || !*cmd) return false;
	const char *s = strcasestr(known_commands(e), cmd);
	if (!s) return false;
	if (s != known_commands(e) && s[-1] != '\n') return false;

	int len = strlen(cmd);
	return s[len] == '\0' || s[len] == '\n';
}

/* Add move to gtp move history. */
static void
gtp_add_move(gtp_t *gtp, move_t *m)
{
	assert(gtp->moves < (int)(sizeof(gtp->move) / sizeof(gtp->move[0])));
	gtp->move[gtp->moves++] = *m;
}

static int
gtp_board_play(gtp_t *gtp, board_t *b, move_t *m)
{
	int r = board_play(b, m);
	if (r < 0)  return r;
	
	/* Add to gtp move history. */
	gtp_add_move(gtp, m);
	
	return r;
}

static enum parse_code
cmd_protocol_version(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	gtp_reply(gtp, "2", NULL);
	return P_OK;
}

static enum parse_code
cmd_name(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *name = "Pachi %s";
	if (!strcmp(e->name, "UCT"))  name = "Pachi";
	if (gtp->custom_name)         name = gtp->custom_name;
	gtp_reply_printf(gtp, name, e->name);
	return P_OK;
}

static enum parse_code
cmd_echo(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	gtp_reply(gtp, gtp->next, NULL);
	return P_OK;
}

/* Return engine comment if playing on kgs, Pachi version otherwise.
 * See "banner" uct param to set engine comment. */
static enum parse_code
cmd_version(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* kgs hijacks 'version' gtp command for game start message. */	
	const char *version = (gtp->kgs ? e->comment : "%s");

	/* Custom gtp version ? */
	if (gtp->custom_version)  version = gtp->custom_version;
	
	/* %s in version string stands for Pachi version. */
	gtp_reply_printf(gtp, version, PACHI_VERSION);
	return P_OK;
}

static enum parse_code
cmd_list_commands(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	gtp_reply(gtp, known_commands(engine), NULL);
	return P_OK;
}

static enum parse_code
cmd_known_command(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	if (gtp_is_valid(engine, arg)) {
		gtp_reply(gtp, "true", NULL);
	} else {
		gtp_reply(gtp, "false", NULL);
	}
	return P_OK;
}

static enum parse_code
cmd_quit(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	gtp_reply(gtp, NULL);
	pachi_done();
	exit(0);
}

static enum parse_code
cmd_boardsize(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	int size = atoi(arg);

	/* Give sane error msg if pachi was compiled for a specific board size. */
#ifdef BOARD_SIZE
	if (size != BOARD_SIZE) {
		gtp_error_printf(gtp, "This Pachi only plays on %ix%i.\n", BOARD_SIZE, BOARD_SIZE);
		die("Yozaa ! This Pachi only plays on %ix%i.\n", BOARD_SIZE, BOARD_SIZE);
	}
#endif
		    
	if (size < 1 || size > BOARD_MAX_SIZE) {
		gtp_error(gtp, "illegal board size", NULL);
		return P_OK;
	}
	board_resize(board, size);
	board_clear(board);
	return P_ENGINE_RESET;
}

static enum parse_code
cmd_clear_board(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	board_clear(board);
	gtp->played_games++;
	if (DEBUGL(3) && debug_boardprint)
		board_print(board, stderr);

	/* Reset move history. */
	gtp->moves = 0;
	
	return P_ENGINE_RESET;
}

static enum parse_code
cmd_kgs_game_over(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	/* The game may not be really over, just adjourned.
	 * Do not clear the board to avoid illegal moves
	 * if the game is resumed immediately after. KGS
	 * may start directly with genmove on resumption. */
	if (DEBUGL(1)) {
		fprintf(stderr, "game is over\n");
		fflush(stderr);
	}
	if (engine->stop)
		engine->stop(engine);
	/* Sleep before replying, so that kgs doesn't
	 * start another game immediately. */
	sleep(GAME_OVER_SLEEP);
	return P_OK;
}

static enum parse_code
cmd_komi(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	sscanf(arg, PRIfloating, &board->komi);

	if (DEBUGL(3) && debug_boardprint)
		board_print(board, stderr);
	return P_OK;
}

static enum parse_code
cmd_kgs_rules(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);

	if (DEBUGL(2))  fprintf(stderr, "%s\n", time_str());
	
	if (forced_ruleset) {
		if (DEBUGL(2))  fprintf(stderr, "ignored kgs-rules, using %s.\n", forced_ruleset);
		return P_OK;
	}

	if (!board_set_rules(board, arg))
		gtp_error(gtp, "unknown rules", NULL);
	return P_OK;
}

static enum parse_code
cmd_play(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	move_t m;

	char *arg;
	next_tok(arg);
	m.color = str2stone(arg);
	next_tok(arg);
	m.coord = str2coord(arg);
	arg = gtp->next;
	char *enginearg = arg;
	char *reply = NULL;

	// This is where kgs starts the timer, not at genmove!
	time_start_timer(&ti[stone_other(m.color)]);

	// XXX engine getting notified if move is illegal !
	if (e->notify_play)
		reply = e->notify_play(e, b, &m, enginearg);
	
	if (gtp_board_play(gtp, b, &m) < 0) {
		if (DEBUGL(0)) {
			fprintf(stderr, "! ILLEGAL MOVE %s %s\n", stone2str(m.color), coord2sstr(m.coord));
			board_print(b, stderr);
		}
		gtp_error(gtp, "illegal move", NULL);
		return P_OK;
	}

	if (DEBUGL(4) && debug_boardprint)
		engine_board_print(e, b, stderr);
	
	gtp_reply(gtp, reply, NULL);
	return P_OK;
}

static enum parse_code
cmd_pachi_predict(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	move_t m;
	char *arg;
	next_tok(arg);
	m.color = str2stone(arg);
	next_tok(arg);
	m.coord = str2coord(arg);
	next_tok(arg);

	char *str = predict_move(board, engine, ti, &m, gtp->played_games);

	/* Add to gtp move history. */
	gtp_add_move(gtp, &m);
	
	gtp_reply(gtp, str, NULL);
	free(str);
	return P_OK;
}

static enum parse_code
cmd_genmove(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	if (DEBUGL(2) && debug_boardprint)
		engine_board_print(e, b, stderr);
		
	if (!ti[color].len.t.timer_start)    /* First game move. */
		time_start_timer(&ti[color]);
	
#ifdef PACHI_FIFO   /* Coordinate between multiple Pachi instances. */
	double time_wait = time_now();
	int ticket = fifo_task_queue();
	double time_start = time_now();
#endif

	time_info_t *ti_genmove = time_info_genmove(b, ti, color);
	coord_t c = (b->fbook ? fbook_check(b) : pass);
	if (is_pass(c))
		c = e->genmove(e, b, ti_genmove, color, !strcasecmp(gtp->cmd, "kgs-genmove_cleanup"));

#ifdef PACHI_FIFO	
	if (DEBUGL(2)) fprintf(stderr, "fifo: genmove in %0.2fs  (waited %0.1fs)\n", time_now() - time_start, time_start - time_wait);
	fifo_task_done(ticket);
#endif

	if (!is_resign(c)) {
		move_t m = move(c, color);
		if (gtp_board_play(gtp, b, &m) < 0)
			die("Attempted to generate an illegal move: %s %s\n", stone2str(m.color), coord2sstr(m.coord));
	}
	
	char *str = coord2sstr(c);
	if (DEBUGL(4))                      fprintf(stderr, "playing move %s\n", str);
	if (DEBUGL(1) && debug_boardprint)  engine_board_print(e, b, stderr);
	gtp_reply(gtp, str, NULL);

	/* Account for spent time. If our GTP peer keeps our clock, this will
	 * be overriden by next time_left GTP command properly. */
	/* (XXX: Except if we pass to byoyomi and the peer doesn't, but that
	 * should be absolutely rare situation and we will just spend a little
	 * less time than we could on next few moves.) */
	if (ti[color].period != TT_NULL && ti[color].dim == TD_WALLTIME)
		time_sub(&ti[color], time_now() - ti[color].len.t.timer_start, true);

	return P_OK;
}

static enum parse_code
cmd_pachi_genmoves(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	void *stats;
	int stats_size;

	time_info_t *ti_genmove = time_info_genmove(board, ti, color);
	char *reply = engine->genmoves(engine, board, ti_genmove, color, gtp->next,
				       !strcasecmp(gtp->cmd, "pachi-genmoves_cleanup"),
				       &stats, &stats_size);
	if (!reply) {
		gtp_error(gtp, "genmoves error", NULL);
		return P_OK;
	}
	if (DEBUGL(3))
		fprintf(stderr, "proposing moves %s\n", reply);
	if (DEBUGL(4) && debug_boardprint)
		engine_board_print(engine, board, stderr);
	gtp_reply(gtp, reply, NULL);
	if (stats_size > 0) {
		double start = time_now();
		fwrite(stats, 1, stats_size, stdout);
		fflush(stdout);
		if (DEBUGVV(2))
			fprintf(stderr, "sent reply %d bytes in %.4fms\n",
				stats_size, (time_now() - start)*1000);
	}
	return P_OK;
}

/* Start tree search in the background and output stats for the sake of frontend running Pachi.
 * Sortof like pondering without a genmove.
 * Stop processing when we receive some other command or "pachi-analyze 0".
 * Similar to Leela-Zero's lz-analyze so we can feed data to lizzie. */
static enum parse_code
cmd_pachi_analyze(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);

	int start = 1;
	if (isdigit(*arg))  start = atoi(arg);
	
	enum stone color = S_BLACK;
	if (last_move(b).color != S_NONE)
		color = stone_other(last_move(b).color);
	
	if (e->analyze) {  e->analyze(e, b, color, start);  gtp->analyze_running = true;  }
	else               gtp_error(gtp, "pachi-analyze not supported for this engine", NULL);
	
	return P_OK;	
}

static void
stop_analyzing(gtp_t *gtp, board_t *b, engine_t *e)
{
	gtp->analyze_running = false;
	e->analyze(e, b, S_BLACK, 0);
}

static enum parse_code
cmd_set_free_handicap(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	do {
		move_t m = move(str2coord(arg), S_BLACK);
		if (DEBUGL(4))  fprintf(stderr, "setting handicap %s\n", arg);

		// XXX board left in inconsistent state if illegal move comes in
		if (gtp_board_play(gtp, b, &m) < 0) {
			if (DEBUGL(0))  fprintf(stderr, "! ILLEGAL MOVE %s\n", arg);
			gtp_error(gtp, "illegal move", NULL);
		}
		
		b->handicap++;
		next_tok(arg);
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
	next_tok(arg);
	int stones = atoi(arg);
	
	strbuf(buf, 1024);	
	move_queue_t q;  mq_init(&q);
	board_handicap(b, stones, &q);
	
	if (DEBUGL(1) && debug_boardprint)
		board_print(b, stderr);

	for (unsigned int i = 0; i < q.moves; i++) {
		move_t m = move(q.move[i], S_BLACK);
		sbprintf(buf, "%s ", coord2sstr(m.coord));

		/* Add to gtp move history. */
		gtp_add_move(gtp, &m);
	}
	
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

static enum parse_code
cmd_final_score(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *msg = NULL;
	ownermap_t *o = engine_ownermap(e, b);
	if (o && !board_position_final(b, o, &msg)) {
		gtp_error(gtp, msg, NULL);	
		return P_OK;
	}

	move_queue_t q;  mq_init(&q);
	if (e->dead_group_list)  e->dead_group_list(e, b, &q);	
	floating_t score = board_official_score(b, &q);

	if (DEBUGL(1))  fprintf(stderr, "counted score %.1f\n", score);
	
	if      (score == 0) gtp_reply_printf(gtp, "0");
	else if (score > 0)  gtp_reply_printf(gtp, "W+%.1f", score);
	else                 gtp_reply_printf(gtp, "B+%.1f", -score);
	return P_OK;
}

static enum parse_code
cmd_pachi_score_est(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap)  {  gtp_error(gtp, "no ownermap", NULL);  return P_OK;  }

	board_print_ownermap(b, stderr, ownermap);
	gtp_reply(gtp, ownermap_score_est_str(b, ownermap), NULL);
	return P_OK;
}

static int
cmd_final_status_list_dead(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	move_queue_t q;  mq_init(&q);
	if (e->dead_group_list)  e->dead_group_list(e, b, &q);
	/* else we return empty list - i.e. engine not supporting
	 * this assumes all stones alive at the game end. */

	gtp_prefix('=', gtp);
	for (unsigned int i = 0; i < q.moves; i++) {
		foreach_in_group(b, q.move[i]) {
			printf("%s ", coord2sstr(c));
		} foreach_in_group_end;
		putchar('\n');
	}
	return q.moves;
}

static int
cmd_final_status_list_alive(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	move_queue_t q;  mq_init(&q);
	if (e->dead_group_list)  e->dead_group_list(e, b, &q);
	int printed = 0;
	
	gtp_prefix('=', gtp);
	foreach_point(b) { // foreach_group, effectively
		group_t g = group_at(b, c);
		if (!g || g != c) continue;

		for (unsigned int i = 0; i < q.moves; i++)
			if (q.move[i] == g)  goto next_group;
			
		foreach_in_group(b, g) {
			printf("%s ", coord2sstr(c));
		} foreach_in_group_end;
		putchar('\n');  printed++;
	next_group:;
	} foreach_point_end;
	return printed;
}

static int
cmd_final_status_list_seki(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap) {  gtp_error(gtp, "no ownermap", NULL);  return -1;  }
	int printed = 0;

	gtp_prefix('=', gtp);
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

	for (unsigned int i = 0; i < sekis.moves; i++) {
		foreach_in_group(b, sekis.move[i]) {
			printf("%s ", coord2sstr(c));
		} foreach_in_group_end;
		putchar('\n');  printed++;
	}

	return printed;
}

static int
cmd_final_status_list_territory(char *arg, board_t *b, engine_t *e, gtp_t *gtp)
{
	enum stone color = str2stone(arg);
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap) {  gtp_error(gtp, "no ownermap", NULL);  return -1;  }
		
	gtp_prefix('=', gtp);
	foreach_point(b) {
		if (board_at(b, c) != S_NONE)  continue;
		if (ownermap_color(ownermap, c, 0.67) != color)  continue;
		printf("%s ", coord2sstr(c));
	} foreach_point_end;
	putchar('\n');
	return 1;
}	

/* XXX: This is a huge hack. */
static enum parse_code
cmd_final_status_list(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (gtp->quiet)  return P_OK;
	char *arg;
	next_tok(arg);
	int r = -1;
	
	if      (!strcasecmp(arg, "dead"))            r = cmd_final_status_list_dead(arg, b, e, gtp);
	else if (!strcasecmp(arg, "alive"))           r = cmd_final_status_list_alive(arg, b, e, gtp);
	else if (!strcasecmp(arg, "seki"))            r = cmd_final_status_list_seki(arg, b, e, gtp);
	else if (!strcasecmp(arg, "black_territory")  ||       /* gnugo extensions */
		 !strcasecmp(arg, "white_territory")) r = cmd_final_status_list_territory(arg, b, e, gtp);
	else    gtp_error(gtp, "illegal status specifier", NULL);

	if (r < 0)  return P_OK;
	if (!r)  putchar('\n');
	gtp_flush();
	return P_OK;
}

/* Handle undo at the gtp level. */
static enum parse_code
cmd_undo(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* --noundo: undo only allowed for pass. */
	if (gtp->noundo && !is_pass(last_move(b).coord)) {
		if (DEBUGL(1))  fprintf(stderr, "undo on non-pass move %s\n", coord2sstr(last_move(b).coord));
		gtp_error(gtp, "cannot undo", NULL);
		return P_OK;
	}
	
	if (!gtp->moves) {  gtp_error(gtp, "no moves to undo", NULL);  return P_OK;  }
	if (b->moves == b->handicap) {  gtp_error(gtp, "can't undo handicap", NULL);  return P_OK;  }
	gtp->moves--;
	
	/* Send a play command to engine so it stops pondering (if it was pondering).  */
	enum stone color = stone_other(last_move(b).color);
	move_t m = move(pass, color);
	if (e->notify_play)
		e->notify_play(e, b, &m, "");

	/* Wait for non-undo command to reset engine. */
	gtp->undo_pending = true;
	
	return P_OK;
}

static void
undo_reload_engine(gtp_t *gtp, board_t *b, engine_t *e, char *e_arg)
{
	if (DEBUGL(3)) fprintf(stderr, "reloading engine after undo(s).\n");
	
	gtp->undo_pending = false;

	engine_reset(e, b, e_arg);
	
	/* Reset board */
	int handicap = b->handicap;
	board_clear(b);
	b->handicap = handicap;

	for (int i = 0; i < gtp->moves; i++) {
		if (e->notify_play)
			e->notify_play(e, b, &gtp->move[i], "");
		int r = board_play(b, &gtp->move[i]);
		assert(r >= 0);
	}
}

static enum parse_code
cmd_showboard(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	board_print(board, stderr);
	return P_OK;
}

/* Custom commands for handling the tree opening tbook */
static enum parse_code
cmd_pachi_gentbook(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	/* Board must be initialized properly, as if for genmove;
	 * makes sense only as 'uct_gentbook b'. */
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	if (!uct_gentbook(engine, board, &ti[color], color))
		gtp_error(gtp, "error generating tbook", NULL);
	return P_OK;
}

static enum parse_code
cmd_pachi_dumptbook(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	uct_dumptbook(engine, board, color);
	return P_OK;
}

static enum parse_code
cmd_pachi_evaluate(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);

	if (!engine->evaluate) {
		gtp_error(gtp, "pachi-evaluate not supported by engine", NULL);
	} else {
		gtp_prefix('=', gtp);
		floating_t vals[board->flen];
		engine->evaluate(engine, board, &ti[color], vals, color);
		for (int i = 0; i < board->flen; i++) {
			if (!board_coord_in_symmetry(board, board->f[i])
			    || isnan(vals[i]) || vals[i] < 0.001)
				continue;
			printf("%s %.3f\n", coord2sstr(board->f[i]), (double) vals[i]);
		}
		gtp_flush();
	}
	return P_OK;
}

static enum parse_code
cmd_pachi_result(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	/* More detailed result of the last genmove. */
	/* For UCT, the output format is: = color move playouts winrate dynkomi */
	char *reply = NULL;
	if (engine->result)
		reply = engine->result(engine, board);
	if (reply)
		gtp_reply(gtp, reply, NULL);
	else
		gtp_error(gtp, "unknown pachi-result command", NULL);
	return P_OK;
}

static enum parse_code
cmd_pachi_tunit(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	int res = unit_test_cmd(board, gtp->next);
	const char *str = (res ? "passed" : "failed");
	gtp_reply(gtp, str, NULL);
	return P_OK;
}

static enum parse_code
cmd_kgs_chat(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *loc;
	next_tok(loc);
	bool opponent = !strcasecmp(loc, "game");
	char *from;
	next_tok(from);
	char *msg = gtp->next;
	msg += strspn(msg, " \n\t");
	char *end = strchr(msg, '\n');
	if (end) *end = '\0';
	char *reply = NULL;
	if (engine->chat) {
		reply = engine->chat(engine, board, opponent, from, msg);
	}
	if (reply)
		gtp_reply(gtp, reply, NULL);
	else
		gtp_error(gtp, "unknown kgs-chat command", NULL);
	return P_OK;
}

static enum parse_code
cmd_time_left(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	next_tok(arg);
	int time = atoi(arg);
	next_tok(arg);
	int stones = atoi(arg);
	if (!ti[color].ignore_gtp)
		time_left(&ti[color], time, stones);
	else
		if (DEBUGL(2)) fprintf(stderr, "ignored time info\n");
	return P_OK;
}

static enum parse_code
cmd_kgs_time_settings(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp)
{
	char *time_system;
	char *arg;
	if (!strcasecmp(gtp->cmd, "kgs-time_settings")) {
		next_tok(time_system);
	} else {
		time_system = "canadian";
	}

	int main_time = 0, byoyomi_time = 0, byoyomi_stones = 0, byoyomi_periods = 0;
	if (!strcasecmp(time_system, "none")) {
		main_time = -1;
	} else if (!strcasecmp(time_system, "absolute")) {
		next_tok(arg);
		main_time = atoi(arg);
	} else if (!strcasecmp(time_system, "byoyomi")) {
		next_tok(arg);
		main_time = atoi(arg);
		next_tok(arg);
		byoyomi_time = atoi(arg);
		next_tok(arg);
		byoyomi_periods = atoi(arg);
	} else if (!strcasecmp(time_system, "canadian")) {
		next_tok(arg);
		main_time = atoi(arg);
		next_tok(arg);
		byoyomi_time = atoi(arg);
		next_tok(arg);
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
	{ "pachi-analyze",          cmd_pachi_analyze },
	{ "lz-analyze",             cmd_pachi_analyze },     /* For Lizzie */

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

	{ 0, 0 }
};

static __attribute__((constructor)) void
gtp_internal_init()
{
	commands = gtp_commands;  /* c++ madness */
}

/* XXX: THIS IS TOTALLY INSECURE!!!!
 * Even basic input checking is missing. */

enum parse_code
gtp_parse(gtp_t *gtp, board_t *b, engine_t *e, char *e_arg, time_info_t *ti, char *buf)
{
	if (strchr(buf, '#'))
		*strchr(buf, '#') = 0;

	/* Reset non global fields. */
	gtp->id = -1;
	gtp->next = buf;
	gtp->replied = false;
	next_tok(gtp->cmd);
	
	if (isdigit(*gtp->cmd)) {
		gtp->id = atoi(gtp->cmd);
		next_tok(gtp->cmd);
	}

	if (!*gtp->cmd)
		return P_OK;

	if (gtp->analyze_running && strcasecmp(gtp->cmd, "pachi-analyze"))
		stop_analyzing(gtp, b, e);
	
	/* Undo: reload engine after first non-undo command. */
	if (gtp->undo_pending && strcasecmp(gtp->cmd, "undo"))
		undo_reload_engine(gtp, b, e, e_arg);
	
	if (e->notify && gtp_is_valid(e, gtp->cmd)) {
		char *reply;
		enum parse_code c = e->notify(e, b, gtp->id, gtp->cmd, gtp->next, &reply);
		if (c == P_NOREPLY) {
			gtp->quiet = true;
		} else if (c == P_DONE_OK) {
			gtp_reply(gtp, reply, NULL);
			return P_OK;
		} else if (c == P_DONE_ERROR) {
			gtp_error(gtp, reply, NULL);
			/* This is an internal error for the engine, but
			 * it is still OK from main's point of view. */
			return P_OK;
		} else if (c != P_OK)
			return c;
	}
	
	for (int i = 0; commands[i].cmd; i++)
		if (!strcasecmp(gtp->cmd, commands[i].cmd)) {
			enum parse_code ret = commands[i].f(b, e, ti, gtp);
			/* For functions convenience: no reply means empty reply */
			if (!gtp->replied)
				gtp_reply(gtp, NULL);
			return ret;
		}
	
	gtp_error(gtp, "unknown command", NULL);
	return P_UNKNOWN_COMMAND;
}



