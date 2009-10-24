#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "gtp.h"
#include "mq.h"
#include "uct/uct.h"
#include "version.h"

void
gtp_prefix(char prefix, int id)
{
	if (id >= 0)
		printf("%c%d ", prefix, id);
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
gtp_output(char prefix, int id, va_list params)
{
	gtp_prefix(prefix, id);
	char *s;
	while ((s = va_arg(params, char *))) {
		fputs(s, stdout);
	}
	putchar('\n');
	gtp_flush();
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
		gtp_reply(id, "Pachi ", engine->name, NULL);

	} else if (!strcasecmp(cmd, "version")) {
		gtp_reply(id, PACHI_VERSION, ": ", engine->comment, NULL);

		/* TODO: known_command */

	} else if (!strcasecmp(cmd, "list_commands")) {
		gtp_reply(id, "protocol_version\nname\nversion\nlist_commands\nquit\nboardsize\nclear_board\nkomi\nplay\ngenmove\nset_free_handicap\nplace_free_handicap\nfinal_status_list", NULL);

	} else if (!strcasecmp(cmd, "quit")) {
		gtp_reply(id, NULL);
		exit(0);

	} else if (!strcasecmp(cmd, "boardsize")) {
		char *arg;
		next_tok(arg);
		board_resize(board, atoi(arg));

		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "clear_board")) {
		if (board->es) {
			assert(engine->done_board_state);
			engine->done_board_state(engine, board);
		}
		board_clear(board);
		if (DEBUGL(1))
			board_print(board, stderr);
		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "komi")) {
		char *arg;
		next_tok(arg);
		sscanf(arg, "%f", &board->komi);

		if (DEBUGL(1))
			board_print(board, stderr);
		gtp_reply(id, NULL);

	} else if (!strcasecmp(cmd, "play")) {
		struct move m;

		char *arg;
		next_tok(arg);
		m.color = str2stone(arg);
		next_tok(arg);
		coord_t *c = str2coord(arg, board_size(board));
		m.coord = *c; coord_done(c);

		if (DEBUGL(1))
			fprintf(stderr, "got move %d,%d,%d\n", m.color, coord_x(m.coord, board), coord_y(m.coord, board));
		engine->notify_play(engine, board, &m);
		if (board_play(board, &m) < 0) {
			if (DEBUGL(0)) {
				fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, coord_x(m.coord, board), coord_y(m.coord, board));
				board_print(board, stderr);
			}
			gtp_error(id, "illegal move", NULL);
		} else {
			if (DEBUGL(1))
				board_print(board, stderr);
			gtp_reply(id, NULL);
		}

	} else if (!strcasecmp(cmd, "genmove")) {
		char *arg;
		next_tok(arg);
		enum stone color = str2stone(arg);
		coord_t *c = engine->genmove(engine, board, color);
		struct move m = { *c, color };
		board_play(board, &m);
		char *str = coord2str(*c, board);
		if (DEBUGL(1))
			fprintf(stderr, "playing move %s\n", str);
		if (DEBUGL(1)) {
			board_print_custom(board, stderr, engine->printhook);
		}
		gtp_reply(id, str, NULL);
		free(str); coord_done(c);

	} else if (!strcasecmp(cmd, "set_free_handicap")) {
		struct move m;
		m.color = S_BLACK;

		char *arg;
		next_tok(arg);
		do {
			coord_t *c = str2coord(arg, board_size(board));
			m.coord = *c; coord_done(c);
			if (DEBUGL(1))
				fprintf(stderr, "setting handicap %d,%d\n", coord_x(m.coord, board), coord_y(m.coord, board));

			if (board_play(board, &m) < 0) {
				if (DEBUGL(0))
					fprintf(stderr, "! ILLEGAL MOVE %d,%d,%d\n", m.color, coord_x(m.coord, board), coord_y(m.coord, board));
				gtp_error(id, "illegal move", NULL);
			}
			board->handicap++;
			next_tok(arg);
		} while (*arg);
		if (DEBUGL(1))
			board_print(board, stderr);
		gtp_reply(id, NULL);

	/* TODO: Engine should choose free handicap; however, it tends to take
	 * overly long to think it all out, and unless it's clever its
	 * handicap stones won't be of much help. ;-) */
	} else if (!strcasecmp(cmd, "place_free_handicap")
	          || !strcasecmp(cmd, "fixed_handicap")) {
		char *arg;
		next_tok(arg);
		int stones = atoi(arg);

		gtp_prefix('=', id);
		board_handicap(board, stones, stdout);
		if (DEBUGL(1))
			board_print(board, stderr);
		putchar('\n');
		gtp_flush();

	} else if (!strcasecmp(cmd, "final_score")) {
		float score = board_official_score(board);
		char str[64];
		if (DEBUGL(1))
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

	/* XXX: This is a huge hack. */
	} else if (!strcasecmp(cmd, "final_status_list")) {
		char *arg;
		next_tok(arg);
		struct move_queue q = { .moves = 0 };
		if (engine->dead_group_list)
			engine->dead_group_list(engine, board, &q);
		/* else we return empty list - i.e. engine not supporting
		 * this assumes all stones alive at the game end. */
		if (!strcasecmp(arg, "dead")) {
			gtp_prefix('=', id);
			for (int i = 0; i < q.moves; i++) {
				foreach_in_group(board, q.move[i]) {
					printf("%s ", coord2sstr(c, board));
				} foreach_in_group_end;
				putchar('\n');
			}
			if (!q.moves)
				putchar('\n');
			gtp_flush();
		} else if (!strcasecmp(arg, "seki") || !strcasecmp(arg, "alive")) {
			gtp_prefix('=', id);
			bool printed_group = false;
			foreach_point(board) { // foreach_group, effectively
				group_t g = group_at(board, c);
				if (!g || g != c) continue;

				for (int i = 0; i < q.moves; i++) {
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
			gtp_error(id, "illegal status specifier", NULL);
		}

	/* Custom commands for handling UCT opening book */
	} else if (!strcasecmp(cmd, "uct_genbook")) {
		/* Board must be initialized properly, as if for genmove;
		 * makes sense only as 'uct_genbook b'. */
		char *arg;
		next_tok(arg);
		enum stone color = str2stone(arg);
		if (uct_genbook(engine, board, color))
			gtp_reply(id, NULL);
		else
			gtp_error(id, "error generating book", NULL);

	} else if (!strcasecmp(cmd, "uct_dumpbook")) {
		char *arg;
		next_tok(arg);
		enum stone color = str2stone(arg);
		uct_dumpbook(engine, board, color);
		gtp_reply(id, NULL);

	} else {
		gtp_error(id, "unknown command", NULL);
	}

#undef next_tok
}
