#ifndef PACHI_GOGUI_H
#define PACHI_GOGUI_H

#include "gtp.h"

/* How many moves to display ? */
#define GOGUI_NBEST 9
#define GOGUI_MANY 30

typedef enum gogui_reporting {
	UR_GOGUI_NONE,
	UR_GOGUI_BEST,
	UR_GOGUI_SEQ,
	UR_GOGUI_WR,
} gogui_reporting_t;

extern enum gogui_reporting gogui_livegfx;

extern char gogui_gfx_buf[];


enum parse_code cmd_gogui_analyze_commands(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_livegfx(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_best_moves(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_winrates(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_influence(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_score_est(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_final_score(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_dcnn_best(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_dcnn_colors(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_dcnn_rating(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_color_palette(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_joseki_moves(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_joseki_show_pattern(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_pattern_best(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_pattern_colors(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_pattern_rating(board_t *board, engine_t *engine, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_pattern_features(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_pattern_gammas(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_show_spatial(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);
enum parse_code cmd_gogui_spatial_size(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp);

void gogui_show_best_moves(FILE *f, board_t *b, enum stone color, coord_t *best_c, float *best_r, int n);
void gogui_show_winrates(FILE *f, board_t *b, enum stone color, coord_t *best_c, float *best_r, int nbest);
void gogui_show_best_seq(FILE *f, board_t *b, enum stone color, coord_t *seq, int n);

#endif

