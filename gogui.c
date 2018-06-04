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


typedef enum {
	GOGUI_BEST_WINRATES,
	GOGUI_BEST_MOVES,
	GOGUI_BEST_COLORS,
} gogui_gfx_t;

typedef enum {
	GOGUI_RESCALE_NONE,
	GOGUI_RESCALE_LINEAR = (1 << 0),
	GOGUI_RESCALE_LOG =    (1 << 1),
} gogui_rescale_t;


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
#ifdef DCNN                            /* board check fake since we're called once on startup ... */
	if (!strcmp(e->name, "UCT") && using_dcnn(b)) {
		sbprintf(buf, "gfx/gfx   DCNN Best Moves/gogui-dcnn_best\n");
		sbprintf(buf, "gfx/gfx   DCNN Color Map/gogui-dcnn_colors\n");
		sbprintf(buf, "gfx/gfx   DCNN Ratings/gogui-dcnn_rating\n");
	}
#endif
	if (!strcmp(e->name, "UCT")) {
		sbprintf(buf, "gfx/Live gfx = Best Moves/gogui-livegfx best_moves\n");
		sbprintf(buf, "gfx/Live gfx = Best Sequence/gogui-livegfx best_seq\n");
		sbprintf(buf, "gfx/Live gfx = Winrates/gogui-livegfx winrates\n");
		sbprintf(buf, "gfx/Live gfx = None/gogui-livegfx\n");
	}

	if (e->dead_group_list)
		sbprintf(buf, "string/          Final Score/final_score\n");

	/* Debugging */
	//sbprintf(buf, "gfx/gfx   Color Palette/gogui-color_palette\n");
	
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

/* Convert HSV colorspace to RGB
 * https://stackoverflow.com/a/6930407 */
static void
hsv2rgb(float h, float s, float v, int *r, int *g, int *b)
{
	float       hh, p, q, t, ff;
	int         i;

	if (s <= 0.0) {       // < is bogus, just shuts up warnings
		*r = v;	 *g = v;  *b = v;  return;
	}
	
	hh = h;
	if (hh >= 360.0)  hh = 0.0;
	hh /= 60.0;
	i = (int)hh;
	ff = hh - i;
	p = v * (1.0 - s);
	q = v * (1.0 - (s * ff));
	t = v * (1.0 - (s * (1.0 - ff)));
	
	switch(i) {
	case 0:  *r = 255.0 * v;  *g = 255.0 * t;  *b = 255.0 * p;  break;
	case 1:  *r = 255.0 * q;  *g = 255.0 * v;  *b = 255.0 * p;  break;
	case 2:	 *r = 255.0 * p;  *g = 255.0 * v;  *b = 255.0 * t;  break;
	case 3:	 *r = 255.0 * p;  *g = 255.0 * q;  *b = 255.0 * v;  break;
	case 4:	 *r = 255.0 * t;  *g = 255.0 * p;  *b = 255.0 * v;  break;
	case 5:
	default: *r = 255.0 * v;  *g = 255.0 * p;  *b = 255.0 * q;  break;
	}
}

static void
value2color(float val, int *r, int *g, int *b)
{
	/* Shrink cyan range, too bright:
	 * val: [ 1.0                                        0.0 ]
	 *   h: [  0                    145           215    242 ]
	 *      [ red....................[.....cyan....]....blue ]  <- linear mapping
	 *      [ .......................[. . . . . . .]....blue ]  <- we want this
	 */
	int h1 = 145, h2 = 215;
	int w = h2 - h1;  /* orig cyan range, 70 */
	int w2 = 20;      /* new one */
			
	float h = (1.0 - val) * (242 - w + w2);
	float s = 1.0;
	float v = 1.0;

	/* Convert fake cyan range, and decrease lightness. */
	if (h1 <= h && h <= h1 + w2) {
		h = h1 + (h - h1) * w / w2;
		int m = w / 2;
		v -= (m - fabsf(h - (h1 + m))) * 0.2 / m;
	} else if (h >= h1 + w2)
		h += w - w2;

	/* Also decrease green range lightness. */
	int h0 = 100;  int m0 = (h2 - h0) / 2;
	if (h0 <= h && h <= h2)
		v -= (m0 - fabsf(h - (h0 + m0))) * 0.2 / m0;
	
	//fprintf(stderr, "h: %i\n", (int)h);
	hsv2rgb(h, s, v, r, g, b);
}


/* Display best moves graphically in GoGui. */
void
gogui_show_best_moves_colors(strbuf_t *buf, struct board *b, enum stone color,
			     coord_t *best_c, float *best_r, int n)
{
	float vals[BOARD_MAX_COORDS];
	for (int i = 0; i < BOARD_MAX_COORDS; i++)
		vals[i] = 0;

	for (int i = 0; i < n; i++)
		if (best_c[i] != pass)
			vals[best_c[i]] = best_r[i];

	for (int y = 19; y >= 1; y--)
	for (int x = 1; x <= 19; x++) {		
		coord_t c = coord_xy(b, x, y);			
		int rr, gg, bb;
		value2color(vals[c], &rr, &gg, &bb);
		
		//fprintf(stderr, "COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c, b));
		sbprintf(buf,   "COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c, b));
	}
}

static void
rescale_best_moves(coord_t *best_c, float *best_r, int n, int rescale)
{
	if (rescale & GOGUI_RESCALE_LINEAR) {
		for (int i = 0; i < n; i++)
			if (best_c[i] == pass) {  n = i; break;  }
		for (int i = 0; i < n; i++) {
			best_r[i] = (float)(n-i)/n;
			//fprintf(stderr, "linear: %i\n", (int)(best_r[i] * 100));
		}
	}       

	if (rescale & GOGUI_RESCALE_LOG) {
		for (int i = 0; i < n; i++)
			if (best_c[i] == pass) {  n = i; break;  }

		float max = log(1.0 * 1000);
		for (int i = 0; i < n; i++) {
			best_r[i] = log(best_r[i] * 1000) / max;
			if (best_r[i] < 0)  best_r[i] = 0.;
			//fprintf(stderr, "log: %i\n", (int)(best_r[i] * 100));
		}
	}	
}

static void
gogui_best_moves(strbuf_t *buf, struct engine *e, struct board *b, struct time_info *ti,
		 enum stone color, int n, gogui_gfx_t gfx_type, gogui_rescale_t rescale)
{
	assert(color != S_NONE);
	struct time_info *ti_genmove = time_info_genmove(b, ti, color);
	
	coord_t best_c[n];
	float   best_r[n];
	for (int i = 0; i < n; i++)  {  best_c[i] = pass;  best_r[i] = 0.;  }
	e->best_moves(e, b, ti_genmove, color, best_c, best_r, n);
	
#if 0
	fprintf(stderr, "best: [");
	for (int i = 0; i < n; i++)
		fprintf(stderr, "%s ", coord2sstr(best_c[i], b));
	fprintf(stderr, "]\n");
#endif
	
	rescale_best_moves(best_c, best_r, n, rescale);
	
	if (gfx_type == GOGUI_BEST_WINRATES)
		gogui_show_winrates(buf, b, color, best_c, best_r, n);
	if (gfx_type == GOGUI_BEST_MOVES)
		gogui_show_best_moves(buf, b, color, best_c, best_r, n);
	if (gfx_type == GOGUI_BEST_COLORS)
		gogui_show_best_moves_colors(buf, b, color, best_c, best_r, n);
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
cmd_gogui_color_palette(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	float   best_r[GOGUI_CANDIDATES] = { 0.0, };
	coord_t best_c[GOGUI_CANDIDATES];
	for (int i = 0; i < GOGUI_CANDIDATES; i++)
		best_c[i] = coord_xy(b, i%19 +1, 18 - i/19 + 1);

	rescale_best_moves(best_c, best_r, GOGUI_CANDIDATES, GOGUI_RESCALE_LINEAR);	
	gogui_show_best_moves_colors(buf, b, color, best_c, best_r, GOGUI_CANDIDATES);
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
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
	
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	int prev = gogui_livegfx;
	gogui_set_livegfx(e, "winrates");
	gogui_best_moves(buf, e, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_WINRATES, 0);
	gogui_livegfx = prev;

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_best_moves(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	
	int prev = gogui_livegfx;
	gogui_set_livegfx(e, "best_moves");
	gogui_best_moves(buf, e, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_MOVES, 0);
	gogui_livegfx = prev;
	
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

#ifdef DCNN
static struct engine *dcnn_engine = NULL;

enum parse_code
cmd_gogui_dcnn_best(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_reply(gtp, "TEXT Not using dcnn", NULL);  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = engine_dcnn_init("", b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, dcnn_engine, b, ti, color, 10, GOGUI_BEST_MOVES, 0);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_dcnn_colors(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_reply(gtp, "TEXT Not using dcnn", NULL);  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = engine_dcnn_init("", b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, dcnn_engine, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_COLORS, GOGUI_RESCALE_LOG);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_dcnn_rating(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_reply(gtp, "TEXT Not using dcnn", NULL);  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = engine_dcnn_init("", b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, dcnn_engine, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_WINRATES, 0);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

#endif /* DCNN */

