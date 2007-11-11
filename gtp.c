#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "gtp.h"

void
gtp_output(char prefix, int id, va_list params)
{
	if (id >= 0)
		printf("%c%d ", prefix, id);
	else
		printf("%c ", prefix);

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
		gtp_reply(id, "ZZGo ", engine->name, NULL);

	} else if (!strcasecmp(cmd, "version")) {
		gtp_reply(id, NULL);

		/* TODO: known_command */

	} else if (!strcasecmp(cmd, "list_commands")) {
		gtp_reply(id, "protocol_version\nname\nversion\nlist_commands\nquit\nboardsize\nclear_board\nkomi\nplay\ngenmove\n", NULL);

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
		struct coord *c = str2coord(arg);
		m.coord = *c; coord_done(c);

		//fprintf(stderr, "got move %d,%d,%d\n", m.color, m.coord.x, m.coord.y);
		if (!board_play(board, &m)) {
			fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, m.coord.x, m.coord.y);
			gtp_error(id, "illegal move", NULL);
		} else {
			gtp_reply(id, NULL);
		}

	} else if (!strcasecmp(cmd, "genmove")) {
		char *arg;
		next_tok(arg);
		enum stone color = str2stone(arg);
		struct coord *c = engine->genmove(board, color);
		struct move m = { *c, color };
		board_play(board, &m);
		char *str = coord2str(*c);
		//fprintf(stderr, "playing move %s\n", str);
		gtp_reply(id, str, NULL);
		free(str); coord_done(c);

	} else {
		gtp_error(id, "unknown command", NULL);
	}

#undef next_tok
}
