#ifndef PACHI_GOGUI_H
#define PACHI_GOGUI_H

/* How many candidates to display */
#define GOGUI_CANDIDATES 5

enum gogui_reporting {
	UR_GOGUI_ZERO,
	UR_GOGUI_CAN,
	UR_GOGUI_SEQ,
	UR_GOGUI_WR,
};

extern enum gogui_reporting gogui_live_gfx;

extern char gogui_gfx_buf[];


enum parse_code cmd_gogui_analyze_commands(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_live_gfx(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_owner_map(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_best_moves(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_winrates(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);

#endif

