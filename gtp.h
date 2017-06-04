#ifndef PACHI_GTP_H
#define PACHI_GTP_H

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

typedef struct
{
	char *cmd;
	char *next;
	int   id;
	int   replied;
} gtp_t;

#define next_tok(to_) \
	to_ = gtp->next; \
	gtp->next = gtp->next + strcspn(gtp->next, " \t\r\n"); \
	if (*gtp->next) { \
		*gtp->next = 0; gtp->next++; \
		gtp->next += strspn(gtp->next, " \t\r\n"); \
	}

enum parse_code gtp_parse(struct board *b, struct engine *e, struct time_info *ti, char *buf);
bool gtp_is_valid(struct engine *e, const char *cmd);
void gtp_final_score_str(struct board *board, struct engine *engine, char *reply, int len);
void gtp_reply(gtp_t *gtp, ...);

#define is_gamestart(cmd) (!strcasecmp((cmd), "boardsize"))
#define is_reset(cmd) (is_gamestart(cmd) || !strcasecmp((cmd), "clear_board") || !strcasecmp((cmd), "kgs-rules"))
#define is_repeated(cmd) (strstr((cmd), "pachi-genmoves"))

/* Number of games played so far */
extern int played_games;

#endif
