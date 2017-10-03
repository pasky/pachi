#include <assert.h>
#include <math.h>
#include "board.h"
#include "engine.h"
#include "timeinfo.h"
#include "gtp.h"
#include "gogui.h"
#include "ownermap.h"

#ifdef DCNN
#include "engines/dcnn.h"
#include "dcnn.h"
#endif /* DCNN */

enum parse_code
cmd_gogui_analyze_commands(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char buffer[1000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	if (e->best_moves) {
		sbprintf(buf, "gfx/gfx   Best Moves/gogui-best_moves\n");
		sbprintf(buf, "gfx/gfx   Winrates/gogui-winrates\n");
	}
	if (e->ownermap) {
		sbprintf(buf, "gfx/gfx   Influence/gogui-ownermap\n");
		sbprintf(buf, "gfx/gfx   Score Est/gogui-score_est\n");	
	}

	if (!strcmp(e->name, "UCT")) {
#ifdef DCNN
		sbprintf(buf, "gfx/gfx   DCNN Best Moves/gogui-dcnn_best\n");
		sbprintf(buf, "gfx/gfx   DCNN Ratings/gogui-dcnn_rating\n");
#endif
		sbprintf(buf, "gfx/Live gfx = Best Moves/gogui-livegfx best_moves\n");
		sbprintf(buf, "gfx/Live gfx = Best Sequence/gogui-livegfx best_seq\n");
		sbprintf(buf, "gfx/Live gfx = Winrates/gogui-livegfx winrates\n");
		sbprintf(buf, "gfx/Live gfx = None/gogui-livegfx\n");
	}

	if (e->dead_group_list)
		sbprintf(buf, "string/          Final Score/final_score\n");
	
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}


enum gogui_reporting gogui_livegfx = 0;

static void
gogui_set_livegfx(struct engine *e, char *arg)
{
	gogui_livegfx = 0;
	if (!strcmp(arg, "best_moves"))
		gogui_livegfx = UR_GOGUI_BEST;
	if (!strcmp(arg, "best_seq"))
		gogui_livegfx = UR_GOGUI_SEQ;
	if (!strcmp(arg, "winrates"))
		gogui_livegfx = UR_GOGUI_WR;
	if (e->livegfx_hook)
		e->livegfx_hook(e);
}

/* GoGui reads live gfx commands on stderr */
void
gogui_show_livegfx(char *str)
{
	fprintf(stderr, "gogui-gfx:\n");
	fprintf(stderr, "%s", str);
	fprintf(stderr, "\n");
}

void
gogui_show_winrates(strbuf_t *buf, struct board *b, enum stone color, coord_t *best_c, float *best_r, int nbest)
{
	/* best move */
	if (best_c[0] != pass)
		sbprintf(buf, "VAR %s %s\n", 
			 (color == S_WHITE ? "w" : "b"),
			 coord2sstr(best_c[0], b) );
	
	for (int i = 0; i < nbest; i++)
		if (best_c[i] != pass)
			sbprintf(buf, "LABEL %s %i\n", coord2sstr(best_c[i], b),
				 (int)(roundf(best_r[i] * 100)));
}

void
gogui_show_best_seq(strbuf_t *buf, struct board *b, enum stone color, coord_t *seq, int n)
{	
	char *col = "bw";
	sbprintf(buf, "VAR ");
	for (int i = 0; i < n && seq[i] != pass; i++)
		sbprintf(buf, "%c %3s ",
			 col[(i + (color == S_WHITE)) % 2],
			 coord2sstr(seq[i], b));
	sbprintf(buf, "\n");
}

/* Display best moves graphically in GoGui. */
void
gogui_show_best_moves(strbuf_t *buf, struct board *b, enum stone color, coord_t *best_c, float *best_r, int n)
{
	/* best move */
	if (best_c[0] != pass)
		sbprintf(buf, "VAR %s %s\n",
			 (color == S_WHITE ? "w" : "b"),
			 coord2sstr(best_c[0], b) );
	
	for (int i = 1; i < n; i++)
		if (best_c[i] != pass)
			sbprintf(buf, "LABEL %s %i\n", coord2sstr(best_c[i], b), i + 1);
}


static void
gogui_best_moves(strbuf_t *buf, struct engine *e, struct board *b, struct time_info *ti,
		 enum stone color, bool want_winrates)
{
	assert(color != S_NONE);
	struct time_info *ti_genmove = time_info_genmove(b, ti, color);

	float   best_r[GOGUI_CANDIDATES] = { 0.0, };
	coord_t best_c[GOGUI_CANDIDATES];
	for (int i = 0; i < GOGUI_CANDIDATES; i++)
		best_c[i] = pass;
	e->best_moves(e, b, ti_genmove, color, best_c, best_r, GOGUI_CANDIDATES);

#if 0
	fprintf(stderr, "best: [");
	for (int i = 0; i < GOGUI_CANDIDATES; i++)
		fprintf(stderr, "%s ", coord2sstr(best_c[i], b));
	fprintf(stderr, "]\n");
#endif

	if (want_winrates)
		gogui_show_winrates(buf, b, color, best_c, best_r, GOGUI_CANDIDATES);
	else
		gogui_show_best_moves(buf, b, color, best_c, best_r, GOGUI_CANDIDATES);
}

static void
gogui_ownermap(strbuf_t *buf, struct board *b, struct engine *e)
{
	struct board_ownermap *ownermap = (e->ownermap ? e->ownermap(e, b) : NULL);
	if (!ownermap)	return;

	sbprintf(buf, "INFLUENCE");	
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;
		float p = board_ownermap_estimate_point(ownermap, c);
		
		// p = -1 for WHITE, 1 for BLACK absolute ownership of point i
		if (p < -.8)
			p = -1.0;
		else if (p < -.5)
			p = -0.7;
		else if (p < -.2)
			p = -0.4;
		else if (p < 0.2)
			p = 0.0;
		else if (p < 0.5)
			p = 0.4;
		else if (p < 0.8)
			p = 0.7;
		else
			p = 1.0;
		sbprintf(buf, " %3s %.1lf", coord2sstr(c, b), p);
	} foreach_point_end;

	sbprintf(buf, "\nTEXT Score Est: %s", board_ownermap_score_est_str(b, ownermap));
}

static void
gogui_score_est(strbuf_t *buf, struct board *b, struct engine *e)
{
	struct board_ownermap *ownermap = (e->ownermap ? e->ownermap(e, b) : NULL);
	if (!ownermap)	return;

	sbprintf(buf, "INFLUENCE");	
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		enum point_judgement j = board_ownermap_score_est_coord(b, ownermap, c);
		float p = 0;
		if (j == PJ_BLACK)  p = 0.5;
		if (j == PJ_WHITE)  p = -0.5;
		sbprintf(buf, " %3s %.1lf", coord2sstr(c, b), p);
	} foreach_point_end;

	sbprintf(buf, "\nTEXT Score Est: %s", board_ownermap_score_est_str(b, ownermap));
}

enum parse_code
cmd_gogui_livegfx(struct board *board, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	gogui_set_livegfx(e, arg);
	return P_OK;
}

enum parse_code
cmd_gogui_ownermap(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_ownermap(buf, b, e);
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_score_est(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_score_est(buf, b, e);
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_winrates(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[1024];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	int prev = gogui_livegfx;
	gogui_set_livegfx(e, "winrates");
	gogui_best_moves(buf, e, b, ti, color, true);
	gogui_livegfx = prev;

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_best_moves(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[1024];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	
	int prev = gogui_livegfx;
	gogui_set_livegfx(e, "best_moves");
	gogui_best_moves(buf, e, b, ti, color, false);
	gogui_livegfx = prev;
	
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

#ifdef DCNN
static struct engine *dcnn_engine = NULL;

enum parse_code
cmd_gogui_dcnn_best(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!dcnn_engine)   dcnn_engine = engine_dcnn_init("", b);
	if (!using_dcnn(b)) return P_OK;
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[1024];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, dcnn_engine, b, ti, color, false);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_dcnn_rating(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!dcnn_engine)   dcnn_engine = engine_dcnn_init("", b);
	if (!using_dcnn(b)) return P_OK;
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[1024];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, dcnn_engine, b, ti, color, true);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

#endif /* DCNN */

