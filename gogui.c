#include <assert.h>
#include <math.h>
#include "board.h"
#include "engine.h"
#include "timeinfo.h"
#include "gtp.h"
#include "gogui.h"
#include "ownermap.h"
#include "joseki.h"
#include "uct/uct.h"
#include "pattern.h"
#include "engines/patternplay.h"
#include "engines/josekiplay.h"
#include "patternsp.h"
#include "patternprob.h"

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
	char buffer[2000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	if (e->best_moves) {
		sbprintf(buf, "gfx/Best Moves/gogui-best_moves\n");
		sbprintf(buf, "gfx/Winrates/gogui-winrates\n");
	}
	if (e->ownermap) {
		sbprintf(buf, "gfx/Influence/gogui-influence\n");
		sbprintf(buf, "gfx/Score Est/gogui-score_est\n");
	}
	if (e->dead_group_list) {
		sbprintf(buf, "gfx/Final Score/gogui-final_score\n");
		sbprintf(buf, "plist/Dead Groups/final_status_list dead\n");
		//sbprintf(buf, "plist/Final Status List Dead/final_status_list dead\n");
		//sbprintf(buf, "plist/Final Status List Alive/final_status_list alive\n");
		//sbprintf(buf, "plist/Final Status List Seki/final_status_list seki\n");
		//sbprintf(buf, "plist/Final Status List Black/final_status_list black_territory\n");
		//sbprintf(buf, "plist/Final Status List White/final_status_list white_territory\n");
	}
	if (!strcmp(e->name, "UCT") && using_joseki(b))
		sbprintf(buf, "gfx/Joseki Moves/gogui-joseki_moves\n");
		sbprintf(buf, "gfx/Joseki Range/gogui-joseki_show_pattern %%p\n");
#ifdef DCNN                            /* board check fake since we're called once on startup ... */
	if (!strcmp(e->name, "UCT") && using_dcnn(b)) {
		sbprintf(buf, "gfx/DCNN Best Moves/gogui-dcnn_best\n");
		sbprintf(buf, "gfx/DCNN Color Map/gogui-dcnn_colors\n");
		sbprintf(buf, "gfx/DCNN Ratings/gogui-dcnn_rating\n");
	}
#endif
	if (!strcmp(e->name, "UCT") && using_patterns()) {
		sbprintf(buf, "gfx/Pattern Best Moves/gogui-pattern_best\n");
		sbprintf(buf, "gfx/Pattern Color Map/gogui-pattern_colors\n");
		sbprintf(buf, "gfx/Pattern Ratings/gogui-pattern_rating\n");
		sbprintf(buf, "gfx/Pattern Features/gogui-pattern_features %%p\n");
		sbprintf(buf, "gfx/Pattern Gammas/gogui-pattern_gammas %%p\n");
		sbprintf(buf, "gfx/Set Spatial Size/gogui-spatial_size %%o\n");
		sbprintf(buf, "gfx/Show Spatial/gogui-show_spatial %%p\n");
	}
	if (!strcmp(e->name, "UCT")) {
		sbprintf(buf, "gfx/Live gfx = Best Moves/gogui-livegfx best_moves\n");
		sbprintf(buf, "gfx/Live gfx = Best Sequence/gogui-livegfx best_seq\n");
		sbprintf(buf, "gfx/Live gfx = Winrates/gogui-livegfx winrates\n");
		sbprintf(buf, "gfx/Live gfx = None/gogui-livegfx\n");
	}

	/* Debugging */
	//sbprintf(buf, "gfx/Color Palette/gogui-color_palette\n");
	
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}


/****************************************************************************************/
/* Utils */

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

static void
gogui_paint_pattern(struct board *b, int colors[BOARD_MAX_COORDS][4],
		    coord_t coord, unsigned int maxd,
		    int rr, int gg, int bb)
{
	for (unsigned int d = 2; d <= maxd; d++)
	for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, coord, b, j);
		        coord_t c  = coord_xy(b, x, y);
			if (board_at(b, c) == S_OFFBOARD)  continue;

/* Just lighten if already something */
#define add_primary_color(p, val)  colors[c][p] = (val) + (colors[c][p] ? 30 : 0);

			add_primary_color(0, rr);
			add_primary_color(1, gg);
			add_primary_color(2, bb);
			colors[c][3]++; /* count */
	}
}

/* Display spatial pattern */
static void
gogui_show_pattern(struct board *b, strbuf_t *buf, coord_t coord, int maxd)
{
	assert(!is_pass(coord));
	int colors[BOARD_MAX_COORDS][4];  memset(colors, 0, sizeof(colors));
	
	//gogui_paint_pattern(b, colors, coord, maxd, 0x00, 0x8a, 0xff);   // blue
	gogui_paint_pattern(b, colors, coord, maxd, 0xff, 0xa2, 0x00);     // orange

	foreach_point(b) {
		if (!colors[c][3]) continue;
		int rr = MIN(colors[c][0], 255);
		int gg = MIN(colors[c][1], 255);
		int bb = MIN(colors[c][2], 255);
		sbprintf(buf, "COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c, b));
	} foreach_point_end;
}


/****************************************************************************************/

enum gogui_reporting gogui_livegfx = 0;

static void
gogui_set_livegfx(struct engine *e, char *arg)
{
	gogui_livegfx = 0;
	if (!strcmp(arg, "best_moves"))  gogui_livegfx = UR_GOGUI_BEST;
	if (!strcmp(arg, "best_seq"))    gogui_livegfx = UR_GOGUI_SEQ;
	if (!strcmp(arg, "winrates"))    gogui_livegfx = UR_GOGUI_WR;
	if (e->livegfx_hook)  e->livegfx_hook(e);
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
	engine_best_moves(e, b, ti_genmove, color, best_c, best_r, n);
	
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
cmd_gogui_influence(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	struct ownermap *ownermap = engine_ownermap(e, b);
	if (!ownermap)  {  gtp_error(gtp, "no ownermap", NULL);  return P_OK;  }
	
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	
	sbprintf(buf, "INFLUENCE");	
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;
		float p = ownermap_estimate_point(ownermap, c);
		
		// p = -1 for WHITE, 1 for BLACK absolute ownership of point i
		if      (p < -.8)  p = -1.0;
		else if (p < -.5)  p = -0.7;
		else if (p < -.2)  p = -0.4;
		else if (p < 0.2)  p = 0.0;
		else if (p < 0.5)  p = 0.4;
		else if (p < 0.8)  p = 0.7;
		else               p = 1.0;
		sbprintf(buf, " %3s %.1lf", coord2sstr(c, b), p);
	} foreach_point_end;

	sbprintf(buf, "\nTEXT Score Est: %s", ownermap_score_est_str(b, ownermap));
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_score_est(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	struct ownermap *ownermap = engine_ownermap(e, b);
	if (!ownermap)  {  gtp_error(gtp, "no ownermap", NULL);  return P_OK;  }
	
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	
	sbprintf(buf, "INFLUENCE");
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		enum point_judgement j = ownermap_score_est_coord(b, ownermap, c);
		float p = 0;
		if (j == PJ_BLACK)  p = 0.5;
		if (j == PJ_WHITE)  p = -0.5;
		sbprintf(buf, " %3s %.1lf", coord2sstr(c, b), p);
	} foreach_point_end;

	sbprintf(buf, "\nTEXT Score Est: %s", ownermap_score_est_str(b, ownermap));
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_final_score(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char *msg = NULL;
	struct ownermap *o = engine_ownermap(e, b);
	if (o && !board_position_final(b, o, &msg)) {
		gtp_error(gtp, msg, NULL);
		return P_OK;
	}

	struct move_queue q = { .moves = 0 };
	if (e->dead_group_list)  e->dead_group_list(e, b, &q);
	
	int dame, seki;
	int ownermap[board_size2(b)];
	floating_t score = board_official_score_details(b, &q, &dame, &seki, ownermap, NULL);
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	
	sbprintf(buf, "INFLUENCE");
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		float p = 0;
		if (ownermap[c] == S_BLACK)  p = 0.5;
		if (ownermap[c] == S_WHITE)  p = -0.5;
		sbprintf(buf, " %3s %.1lf", coord2sstr(c, b), p);
	} foreach_point_end;
	sbprintf(buf, "\n");
	
	if      (score == 0) sbprintf(buf, "TEXT 0\n");
	else if (score > 0)  sbprintf(buf, "TEXT W+%.1f\n", score);
	else                 sbprintf(buf, "TEXT B+%.1f\n", -score);

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
/********************************************************************************************/
/* dcnn */

static struct engine *dcnn_engine = NULL;

enum parse_code
cmd_gogui_dcnn_best(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_reply(gtp, "TEXT Not using dcnn", NULL);  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = new_engine(E_DCNN, "", b);
	
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
	if (!dcnn_engine)   dcnn_engine = new_engine(E_DCNN, "", b);
	
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
	if (!dcnn_engine)   dcnn_engine = new_engine(E_DCNN, "", b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, dcnn_engine, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_WINRATES, 0);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

#endif /* DCNN */


/********************************************************************************************/
/* joseki */

static struct engine *joseki_engine = NULL;

enum parse_code
cmd_gogui_joseki_moves(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!using_joseki(b)) {  gtp_reply(gtp, "TEXT Not using joseki", NULL);  return P_OK;  }
	if (!joseki_engine)   joseki_engine = new_engine(E_JOSEKIPLAY, NULL, b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	float joseki_map[BOARD_MAX_COORDS];
	joseki_rate_moves(joseki_dict, b, color, joseki_map);

	/* Show relaxed / ignored moves */
	foreach_free_point(b) {
		josekipat_t *p = joseki_lookup_ignored(joseki_dict, b, c, color);
		if (p)  sbprintf(buf, "MARK %s\n", coord2sstr(c, b));
		if (p && (p->flags & JOSEKI_FLAGS_3X3))
			sbprintf(buf, "CIRCLE %s\n", coord2sstr(c, b));
		
		p = joseki_lookup_3x3(joseki_dict, b, c, color);
		if (p)  sbprintf(buf, "CIRCLE %s\n", coord2sstr(c, b));
	} foreach_free_point_end;

	gogui_best_moves(buf, joseki_engine, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_COLORS, GOGUI_RESCALE_LOG);

	/* Show ignored moves, background color */
	foreach_free_point(b) {
		if (joseki_map[c]) continue;  /* Don't clobber valid moves ! */
		josekipat_t *p = joseki_lookup_ignored(joseki_dict, b, c, color);
		if (!p)  continue;
		sbprintf(buf, "COLOR #0000a0 %s\n", coord2sstr(c, b));
	} foreach_free_point_end;

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_joseki_show_pattern(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char *arg;  next_tok(arg);
	if (!arg)                          {  gtp_error(gtp, "arg missing", NULL);  return P_OK;  }
	coord_t coord = str2coord(arg, board_size(b));

	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_show_pattern(b, buf, coord, JOSEKI_PATTERN_DIST);
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}


/********************************************************************************************/
/* pattern */

static struct engine *pattern_engine = NULL;

static void
init_patternplay_engine(struct board *b)
{
	char args[] = "mcowner_fast=0";
	pattern_engine = new_engine(E_PATTERNPLAY, args, b);
}

enum parse_code
cmd_gogui_pattern_best(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_patternplay_engine(b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, pattern_engine, b, ti, color, 10, GOGUI_BEST_MOVES, 0);

	bool locally = patternplay_matched_locally(pattern_engine);
	sbprintf(buf, "TEXT Matching Locally: %s\n", (locally ? "Yes" : "No"));
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_pattern_colors(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_patternplay_engine(b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, pattern_engine, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_COLORS, GOGUI_RESCALE_LOG);

	bool locally = patternplay_matched_locally(pattern_engine);
	sbprintf(buf, "TEXT Matching Locally: %s\n", (locally ? "Yes" : "No"));
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_pattern_rating(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_patternplay_engine(b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char buffer[5000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_best_moves(buf, pattern_engine, b, ti, color, GOGUI_CANDIDATES, GOGUI_BEST_WINRATES, 0);

	bool locally = patternplay_matched_locally(pattern_engine);
	sbprintf(buf, "TEXT Matching Locally: %s\n", (locally ? "Yes" : "No"));
	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

/* Show pattern features on point selected by user. */
enum parse_code
cmd_gogui_pattern_features(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_patternplay_engine(b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char *arg;  next_tok(arg);
	if (!arg)                          {  gtp_error(gtp, "arg missing", NULL);  return P_OK;  }
	coord_t coord = str2coord(arg, board_size(b));	
	if (board_at(b, coord) != S_NONE)  {  gtp_reply(gtp, "TEXT Must be empty spot ...", NULL);  return P_OK;  }
	
	struct ownermap ownermap;
	struct pattern p;
	struct move m = { .coord = coord, .color = color };
	struct pattern_config *pc = patternplay_get_pc(pattern_engine);
	mcowner_playouts(b, color, &ownermap);
	bool locally = pattern_matching_locally(pc, b, color, &ownermap);
	pattern_match(pc, &p, b, &m, &ownermap, locally);

	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	/* Show largest spatial */
	int dist = 0;
	for (int i = 0; i < p.n; i++)
		if (p.f[i].id >= FEAT_SPATIAL3)
			dist = MAX(dist, p.f[i].id - FEAT_SPATIAL3 + 3);
	if (dist)  gogui_show_pattern(b, buf, coord, dist);

	gtp_reply(gtp, buf->str, "TEXT ", pattern2sstr(&p), NULL);
	return P_OK;
}

/* Show pattern gammas on point selected by user. */
enum parse_code
cmd_gogui_pattern_gammas(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_patternplay_engine(b);
	
	enum stone color = S_BLACK;
	if (b->last_move.color)  color = stone_other(b->last_move.color);
	
	char *arg;  next_tok(arg);
	if (!arg)                          {  gtp_error(gtp, "arg missing", NULL);  return P_OK;  }
	coord_t coord = str2coord(arg, board_size(b));	
	if (board_at(b, coord) != S_NONE)  {  gtp_reply(gtp, "TEXT Must be empty spot ...", NULL);  return P_OK;  }
	
	struct ownermap ownermap;
	struct pattern p;
	struct move m = { .coord = coord, .color = color };
	struct pattern_config *pc = patternplay_get_pc(pattern_engine);
	mcowner_playouts(b, color, &ownermap);
	bool locally = pattern_matching_locally(pc, b, color, &ownermap);
	pattern_match(pc, &p, b, &m, &ownermap, locally);

	char buffer[1000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	sbprintf(buf, "TEXT ");
	dump_gammas(buf, pc, &p);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

static int spatial_dist = 6;

enum parse_code
cmd_gogui_show_spatial(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_patternplay_engine(b);
	struct pattern_config *pc = patternplay_get_pc(pattern_engine);

	char *arg;  next_tok(arg);
	if (!arg)                          {  gtp_error(gtp, "arg missing", NULL);  return P_OK;  }
	coord_t coord = str2coord(arg, board_size(b));

	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	gogui_show_pattern(b, buf, coord, spatial_dist);
	
	struct move m = { .coord = coord, .color = stone_other(b->last_move.color) };
	struct spatial s;
	spatial_from_board(pc, &s, b, &m);
	s.dist = spatial_dist;
	spatial_t *s2 = spatial_dict_lookup(spat_dict, s.dist, spatial_hash(0, &s));
	if (s2)	sbprintf(buf, "TEXT matches s%i:%i\n", spatial_dist, spatial_id(s2, spat_dict));
	else	sbprintf(buf, "TEXT unknown s%i spatial\n", spatial_dist);

	spatial_write(spat_dict, &s, 0, stderr);

	gtp_reply(gtp, buf->str, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_spatial_size(struct board *b, struct engine *e, struct time_info *ti, gtp_t *gtp)
{
	char *arg;  next_tok(arg);
	/* Return current value */
	if (!*arg) {  gtp_reply_printf(gtp, "%i", spatial_dist);  return P_OK;  }

	int d = atoi(arg);
	if (d < 3 || d > 10) {  gtp_error(gtp, "Between 3 and 10 please", NULL);  return P_OK;  }
	spatial_dist = d;
	return P_OK;
}
