#ifndef ZZGO_GTP_H
#define ZZGO_GTP_H

struct board;
struct engine;
struct time_info;

enum parse_code {
	P_OK,
	P_NOREPLY,
	P_DONE_OK,
	P_DONE_ERROR,
	P_ENGINE_RESET,
	P_UNKNOWN_COMMAND,
};

enum parse_code gtp_parse(struct board *b, struct engine *e, struct time_info *ti, char *buf);
void gtp_prefix(char prefix, int id);
void gtp_flush(void);

#define is_gamestart(cmd) (!strcasecmp((cmd), "boardsize"))
#define is_reset(cmd) (is_gamestart(cmd) || !strcasecmp((cmd), "clear_board"))

#endif
