#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "gtp.h"

void
gtp_prefix(char prefix, int id)
{
	if (id >= 0)
		printf("%c%d ", prefix, id);
	else
		printf("%c ", prefix);
}

void
gtp_output(char prefix, int id, va_list params)
{
	gtp_prefix(prefix, id);
	char *s;
	while ((s = va_arg(params, char *))) {
		fputs(s, stdout);
	}
	putchar('\n'); putchar('\n');
	fflush(stdout);
}

void
gtp_reply(int id, ...)
{
	va_list params;
	va_start(params, id);
	gtp_output('=', id, params);
	va_end(params);
}

void
gtp_error(int id, ...)
{
	va_list params;
	va_start(params, id);
	gtp_output('?', id, params);
	va_end(params);
}


/* XXX: THIS IS TOTALLY INSECURE!!!!
 * Even basic input checking is missing. */

void
gtp_parse(struct board *board, struct engine *engine, char *buf)
{
#define next_tok(to_) \
	to_ = next; \
	next = next + strcspn(next, " \t\r\n"); \
	if (*next) { \
		*next = 0; next++; \
		next += strspn(next, " \t\r\n"); \
	}

	if (strchr(buf, '#'))
		*strchr(buf, '#') = 0;

	char *cmd, *next = buf;
	next_tok(cmd);

	int id = -1;
	if (isdigit(*cmd)) {
		id = atoi(cmd);
		next_tok(cmd);
	}

	if (!*cmd)
		return;

	if (!strcasecmp(cmd, "protocol_version")) {
		gtp_reply(id, "2", NULL);

	} else if (!strcasecmp(cmd, "name")) {
		/* KGS hack */
		gtp_reply(id, "ZZGo ", engine->name, ": ", engine->comment, NULL);

	} else if (!strcasecmp(cmd, "version")) {
		gtp_reply(id, NULL);

		/* TODO: known_command */

	} else if (!strcasecmp(cmd, "list_commands")) {
		gtp_reply(id, "protocol_version\nname\nversion\nlist_commands\nquit\nboardsize\nclear_board\nkomi\nplay\ngenmove\nset_free_handicap\nplace_free_handicap\n", NULL);

	} else if (!strcasecmp(cmd, "quit")) {
		gtp_reply(id, NULL);
		exit(0);

	} else if (!strcasecmp(cmd, "boardsize")) {
		char *arg;
		next_tok(arg);
		board_resize(board, atoi(arg));

		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "clear_board")) {
		board_clear(board);
		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "komi")) {
		char *arg;
		next_tok(arg);
		sscanf(arg, "%f", &board->komi);

		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "play")) {
		struct move m;

		char *arg;
		next_tok(arg);
		m.color = str2stone(arg);
		next_tok(arg);
		coord_t *c = str2coord(arg, board->size);
		m.coord = *c; coord_done(c);

		if (debug_level > 1)
			fprintf(stderr, "got move %d,%d,%d\n", m.color, coord_x(m.coord), coord_y(m.coord));
		if (board_play(board, &m) < 0) {
			if (debug_level > 0)
				fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, coord_x(m.coord), coord_y(m.coord));
			gtp_error(id, "illegal move", NULL);
		} else {
			gtp_reply(id, NULL);
		}

	} else if (!strcasecmp(cmd, "genmove")) {
		char *arg;
		next_tok(arg);
		enum stone color = str2stone(arg);
		coord_t *c = engine->genmove(engine, board, color);
		struct move m = { *c, color };
		board_play(board, &m);
		char *str = coord2str(*c);
		if (debug_level > 1)
			fprintf(stderr, "playing move %s\n", str);
		gtp_reply(id, str, NULL);
		free(str); coord_done(c);

	} else if (!strcasecmp(cmd, "set_free_handicap")) {
		struct move m;
		m.color = S_BLACK;

		char *arg;
		next_tok(arg);
		do {
			coord_t *c = str2coord(arg, board->size);
			m.coord = *c; coord_done(c);
			if (debug_level > 1)
				fprintf(stderr, "setting handicap %d,%d\n", coord_x(m.coord), coord_y(m.coord));

			if (board_play(board, &m) < 0) {
				if (debug_level > 0)
					fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, coord_x(m.coord), coord_y(m.coord));
				gtp_error(id, "illegal move", NULL);
			}
			next_tok(arg);
		} while (*arg);
		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "place_free_handicap")) {
		char *arg;
		next_tok(arg);
		int stones = atoi(arg);

		gtp_prefix('=', id);
		while (stones--) {
			coord_t *c = engine->genmove(engine, board, S_BLACK);
			struct move m = { *c, S_BLACK };
			board_play(board, &m);
			char *str = coord2str(*c);
			if (debug_level > 1)
				fprintf(stderr, "choosing handicap %s\n", str);
			printf("%s ", str);
			free(str); coord_done(c);
		}
		printf("\n\n"); fflush(stdout);

	} else if (!strcasecmp(cmd, "final_score")) {
		float score = board_official_score(board);
		char str[64];
		if (debug_level > 1)
			fprintf(stderr, "counted score %.1f\n", score);
		if (score == 0) {
			gtp_reply(id, "0", NULL);
		} else if (score > 0) {
			snprintf(str, 64, "W+%.1f", score);
			gtp_reply(id, str, NULL);
		} else {
			snprintf(str, 64, "B+%.1f", -score);
			gtp_reply(id, str, NULL);
		}

	} else {
		gtp_error(id, "unknown command", NULL);
	}

#undef next_tok
}
