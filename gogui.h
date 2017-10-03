#ifndef PACHI_GOGUI_H
#define PACHI_GOGUI_H

/* How many candidates to display */
#define GOGUI_CANDIDATES 5

enum gogui_reporting {
	UR_GOGUI_ZERO,
	UR_GOGUI_BEST,
	UR_GOGUI_SEQ,
	UR_GOGUI_WR,
};

extern enum gogui_reporting gogui_livegfx;

extern char gogui_gfx_buf[];


enum parse_code cmd_gogui_analyze_commands(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_livegfx(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_best_moves(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_winrates(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_ownermap(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_score_est(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_dcnn_best(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);
enum parse_code cmd_gogui_dcnn_rating(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp);


void gogui_show_best_moves(strbuf_t *buf, struct board *b, enum stone color, coord_t *best_c, float *best_r, int n);
void gogui_show_winrates(strbuf_t *buf, struct board *b, enum stone color, coord_t *best_c, float *best_r, int nbest);
void gogui_show_best_seq(strbuf_t *buf, struct board *b, enum stone color, coord_t *seq, int n);
void gogui_show_livegfx(char *str);

#endif

