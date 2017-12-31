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
#include "debug.h"
#include "engine.h"
#include "fbook.h"
#include "gtp.h"
#include "mq.h"
#include "uct/uct.h"
#include "version.h"
#include "timeinfo.h"
#include "gogui.h"
#include "t-predict/predict.h"
#include "t-unit/test.h"

#define NO_REPLY (-2)

/* Sleep 5 seconds after a game ends to give time to kill the program. */
#define GAME_OVER_SLEEP 5

int played_games = 0;

typedef enum parse_code
(*gtp_func_t)(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);

typedef struct
{
	char *cmd;
	gtp_func_t f;
} gtp_command_t;

static gtp_command_t commands[];

void
gtp_prefix(char prefix, gtp_t *gtp)
{
	gtp->replied = true;
	if (gtp->id == NO_REPLY) return;
	if (gtp->id >= 0)
		printf("%c%d ", prefix, gtp->id);
	else
		printf("%c ", prefix);
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
	if (gtp->id == NO_REPLY) return;
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

void
gtp_error(gtp_t *gtp, ...)
{
	va_list params;
	va_start(params, gtp);
	gtp_output('?', gtp, params);
	va_end(params);
}

void
gtp_final_score_str(struct board *board, struct engine *engine, char *reply, int len)
{
	struct move_queue q = { .moves = 0 };
	if (engine->dead_group_list)
		engine->dead_group_list(engine, board, &q);
	floating_t score = board_official_score(board, &q);
	if (DEBUGL(1))
		fprintf(stderr, "counted score %.1f\n", score);
	if (score == 0)
		snprintf(reply, len, "0");
	else if (score > 0)
		snprintf(reply, len, "W+%.1f", score);
	else
		snprintf(reply, len, "B+%.1f", -score);
}

/* List of public gtp commands. The internal command pachi-genmoves is not exported,
 * it should only be used between master and slaves of the distributed engine.
 * For now only uct engine supports gogui-analyze_commands. */
static char*
known_commands(struct engine *engine)
{
	static char buf[8192];
	char *str = buf;
	
	for (int i = 0; commands[i].cmd; i++) {
		char *cmd = commands[i].cmd;		
		if (str_prefix("gogui", cmd) ||
		    str_prefix("pachi-genmoves", cmd))
			continue;		
		str += sprintf(str, "%s\n", commands[i].cmd);
	}
	
	str += sprintf(str, "gogui-analyze_commands\n");
	str[-1] = 0;  /* remove last \n */
	return buf;
}

/* Return true if cmd is a valid gtp command. */
bool
gtp_is_valid(struct engine *e, const char *cmd)
{
	if (!cmd || !*cmd) return false;
	const char *s = strcasestr(known_commands(e), cmd);
	if (!s) return false;
	if (s != known_commands(e) && s[-1] != '\n') return false;

	int len = strlen(cmd);
	return s[len] == '\0' || s[len] == '\n';
}

static enum parse_code
cmd_protocol_version(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	gtp_reply(gtp, "2", NULL);
	return P_OK;
}

static enum parse_code
cmd_name(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	/* KGS hack */
	gtp_reply(gtp, "Pachi ", engine->name, NULL);
	return P_OK;
}

static enum parse_code
cmd_echo(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	gtp_reply(gtp, gtp->next, NULL);
	return P_OK;
}

static enum parse_code
cmd_version(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	gtp_reply(gtp, PACHI_VERSION, ": ", engine->comment, " Have a nice game!", NULL);
	return P_OK;
}

static enum parse_code
cmd_list_commands(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	gtp_reply(gtp, known_commands(engine), NULL);
	return P_OK;
}

static enum parse_code
cmd_known_command(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
cmd_quit(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	gtp_reply(gtp, NULL);
	exit(0);
}

static enum parse_code
cmd_boardsize(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	int size = atoi(arg);
	if (size < 1 || size > BOARD_MAX_SIZE) {
		gtp_error(gtp, "illegal board size", NULL);
		return P_OK;
	}
	board_resize(board, size);
	board_clear(board);
	return P_ENGINE_RESET;
}

static enum parse_code
cmd_clear_board(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	board_clear(board);
	played_games++;
	if (DEBUGL(3) && debug_boardprint)
		board_print(board, stderr);
	return P_ENGINE_RESET;
}

static enum parse_code
cmd_kgs_game_over(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
cmd_komi(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	sscanf(arg, PRIfloating, &board->komi);

	if (DEBUGL(3) && debug_boardprint)
		board_print(board, stderr);
	return P_OK;
}

static enum parse_code
cmd_kgs_rules(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	if (!board_set_rules(board, arg)) {
		gtp_error(gtp, "unknown rules", NULL);
		return P_OK;
	}
	return P_OK;
}

static enum parse_code
cmd_play(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	struct move m;

	char *arg;
	next_tok(arg);
	m.color = str2stone(arg);
	next_tok(arg);
	m.coord = str2coord(arg, board_size(board));
	next_tok(arg);
	char *enginearg = arg;
	char *reply = NULL;

	if (DEBUGL(5))
		fprintf(stderr, "got move %d,%d,%d\n", m.color, coord_x(m.coord, board), coord_y(m.coord, board));

	// This is where kgs starts the timer, not at genmove!
	time_start_timer(&ti[stone_other(m.color)]);

	if (engine->notify_play)
		reply = engine->notify_play(engine, board, &m, enginearg);
	if (board_play(board, &m) < 0) {
		if (DEBUGL(0)) {
			fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, coord_x(m.coord, board), coord_y(m.coord, board));
			board_print(board, stderr);
		}
		gtp_error(gtp, "illegal move", NULL);
	} else {
		if (DEBUGL(4) && debug_boardprint)
			engine_board_print(engine, board, stderr);
		gtp_reply(gtp, reply, NULL);
	}
	return P_OK;
}

static enum parse_code
cmd_pachi_predict(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	struct move m;
	char *arg;
	next_tok(arg);
	m.color = str2stone(arg);
	next_tok(arg);
	m.coord = str2coord(arg, board_size(board));
	next_tok(arg);

	char *str = predict_move(board, engine, ti, &m);
	gtp_reply(gtp, str, NULL);
	free(str);
	return P_OK;
}

static enum parse_code
cmd_genmove(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	if (DEBUGL(2) && debug_boardprint)
		engine_board_print(engine, board, stderr);
		
	if (!ti[color].len.t.timer_start) {
		/* First game move. */
		time_start_timer(&ti[color]);
	}

	struct time_info *ti_genmove = time_info_genmove(board, ti, color);
	coord_t c = (board->fbook ? fbook_check(board) : pass);
	if (is_pass(c))
		c = engine->genmove(engine, board, ti_genmove, color, !strcasecmp(gtp->cmd, "kgs-genmove_cleanup"));
		
	struct move m = { .coord = c, .color = color };
	if (board_play(board, &m) < 0) {
		fprintf(stderr, "Attempted to generate an illegal move: [%s, %s]\n", coord2sstr(m.coord, board), stone2str(m.color));
		abort();
	}
	char *str = coord2sstr(c, board);
	if (DEBUGL(4))
		fprintf(stderr, "playing move %s\n", str);
	if (DEBUGL(1) && debug_boardprint)
		engine_board_print(engine, board, stderr);
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
cmd_pachi_genmoves(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	void *stats;
	int stats_size;

	struct time_info *ti_genmove = time_info_genmove(board, ti, color);
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

static enum parse_code
cmd_set_free_handicap(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	struct move m;
	m.color = S_BLACK;

	char *arg;
	next_tok(arg);
	do {
		m.coord = str2coord(arg, board_size(board));
		if (DEBUGL(4))
			fprintf(stderr, "setting handicap %d,%d\n", coord_x(m.coord, board), coord_y(m.coord, board));

		if (board_play(board, &m) < 0) {
			if (DEBUGL(0))
				fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, coord_x(m.coord, board), coord_y(m.coord, board));
			gtp_error(gtp, "illegal move", NULL);
		}
		board->handicap++;
		next_tok(arg);
	} while (*arg);
	if (DEBUGL(1) && debug_boardprint)
		board_print(board, stderr);
	return P_OK;
}

/* TODO: Engine should choose free handicap; however, it tends to take
 * overly long to think it all out, and unless it's clever its
 * handicap stones won't be of much help. ;-) */
static enum parse_code
cmd_fixed_handicap(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	int stones = atoi(arg);

	gtp_prefix('=', gtp);
	board_handicap(board, stones, gtp->id == NO_REPLY ? NULL : stdout);
	if (DEBUGL(1) && debug_boardprint)
		board_print(board, stderr);
	if (gtp->id == NO_REPLY) return P_OK;
	putchar('\n');
	gtp_flush();
	return P_OK;
}

static enum parse_code
cmd_final_score(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char str[64];
	gtp_final_score_str(board, engine, str, sizeof(str));
	gtp_reply(gtp, str, NULL);
	return P_OK;
}

/* XXX: This is a huge hack. */
static enum parse_code
cmd_final_status_list(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	if (gtp->id == NO_REPLY) return P_OK;
	char *arg;
	next_tok(arg);
	struct move_queue q = { .moves = 0 };
	if (engine->dead_group_list)
		engine->dead_group_list(engine, board, &q);
	/* else we return empty list - i.e. engine not supporting
	 * this assumes all stones alive at the game end. */
	if (!strcasecmp(arg, "dead")) {
		gtp_prefix('=', gtp);
		for (unsigned int i = 0; i < q.moves; i++) {
			foreach_in_group(board, q.move[i]) {
				printf("%s ", coord2sstr(c, board));
			} foreach_in_group_end;
			putchar('\n');
		}
		if (!q.moves)
			putchar('\n');
		gtp_flush();
	} else if (!strcasecmp(arg, "seki") || !strcasecmp(arg, "alive")) {
		gtp_prefix('=', gtp);
		bool printed_group = false;
		foreach_point(board) { // foreach_group, effectively
			group_t g = group_at(board, c);
			if (!g || g != c) continue;

			for (unsigned int i = 0; i < q.moves; i++) {
				if (q.move[i] == g)
					goto next_group;
			}
			foreach_in_group(board, g) {
				printf("%s ", coord2sstr(c, board));
			} foreach_in_group_end;
			putchar('\n');
			printed_group = true;
		next_group:;
		} foreach_point_end;
		if (!printed_group)
			putchar('\n');
		gtp_flush();
	} else {
		gtp_error(gtp, "illegal status specifier", NULL);
	}
	return P_OK;
}


static enum parse_code
cmd_undo(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	if (board_undo(board) < 0) {
		if (DEBUGL(1)) {
			fprintf(stderr, "undo on non-pass move %s\n", coord2sstr(board->last_move.coord, board));
			board_print(board, stderr);
		}
		gtp_error(gtp, "cannot undo", NULL);
		return P_OK;
	}
	char *reply = NULL;
	if (engine->undo)
		reply = engine->undo(engine, board);
	if (DEBUGL(3) && debug_boardprint)
		board_print(board, stderr);
	gtp_reply(gtp, reply, NULL);
	return P_OK;
}

/* Custom commands for handling the tree opening tbook */
static enum parse_code
cmd_pachi_gentbook(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
cmd_pachi_dumptbook(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	uct_dumptbook(engine, board, color);
	return P_OK;
}

static enum parse_code
cmd_pachi_evaluate(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
			printf("%s %.3f\n", coord2sstr(board->f[i], board), (double) vals[i]);
		}
		gtp_flush();
	}
	return P_OK;
}

static enum parse_code
cmd_pachi_result(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
cmd_pachi_tunit(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	int res = unit_test_cmd(board, gtp->next);
	char *str = (res ? "passed" : "failed");
	gtp_reply(gtp, str, NULL);
	return P_OK;
}

static enum parse_code
cmd_kgs_chat(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
cmd_time_left(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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
cmd_kgs_time_settings(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
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


static gtp_command_t commands[] =
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

	/* Short aliases */
	{ "predict",                cmd_pachi_predict },
	{ "tunit",		    cmd_pachi_tunit },

	{ "gogui-analyze_commands", cmd_gogui_analyze_commands },
	{ "gogui-livegfx",          cmd_gogui_livegfx },
	{ "gogui-ownermap",         cmd_gogui_ownermap },
	{ "gogui-score_est",        cmd_gogui_score_est },
	{ "gogui-best_moves",       cmd_gogui_best_moves },
	{ "gogui-winrates",         cmd_gogui_winrates },
#ifdef DCNN
	{ "gogui-dcnn_best",        cmd_gogui_dcnn_best },
	{ "gogui-dcnn_colors",      cmd_gogui_dcnn_colors },
	{ "gogui-dcnn_rating",      cmd_gogui_dcnn_rating },
#endif /* DCNN */
	{ "gogui-color_palette",    cmd_gogui_color_palette },

	{ 0, 0 }
};


/* XXX: THIS IS TOTALLY INSECURE!!!!
 * Even basic input checking is missing. */

enum parse_code
gtp_parse(struct board *board, struct engine *engine, struct time_info *ti, char *buf)
{
	if (strchr(buf, '#'))
		*strchr(buf, '#') = 0;

	gtp_t gtp_struct = { .next = buf, .id = -1 };
	gtp_t *gtp = &gtp_struct;
	next_tok(gtp->cmd);
	
	if (isdigit(*gtp->cmd)) {
		gtp->id = atoi(gtp->cmd);
		next_tok(gtp->cmd);
	}

	if (!*gtp->cmd)
		return P_OK;

	if (engine->notify && gtp_is_valid(engine, gtp->cmd)) {
		char *reply;
		enum parse_code c = engine->notify(engine, board, gtp->id, gtp->cmd, gtp->next, &reply);
		if (c == P_NOREPLY) {
			gtp->id = NO_REPLY;
		} else if (c == P_DONE_OK) {
			gtp_reply(gtp, reply, NULL);
			return P_OK;
		} else if (c == P_DONE_ERROR) {
			gtp_error(gtp, reply, NULL);
			/* This is an internal error for the engine, but
			 * it is still OK from main's point of view. */
			return P_OK;
		} else if (c != P_OK) {
			return c;
		}
	}
	
	for (int i = 0; commands[i].cmd; i++)
		if (!strcasecmp(gtp->cmd, commands[i].cmd)) {
			enum parse_code ret = commands[i].f(board, engine, ti, gtp);
			/* For functions convenience: no reply means empty reply */
			if (!gtp->replied)
				gtp_reply(gtp, NULL);
			return ret;
		}
	
	gtp_error(gtp, "unknown command", NULL);
	return P_UNKNOWN_COMMAND;
}
