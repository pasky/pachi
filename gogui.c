#define DEBUG
#include <assert.h>
#include <math.h>
#include "board.h"
#include "engine.h"
#include "timeinfo.h"
#include "gtp.h"
#include "gogui.h"
#include "josekifix/josekifix.h"
#include "ownermap.h"
#include "joseki/joseki.h"
#include "uct/uct.h"
#include "pattern/pattern.h"
#include "pattern/pattern_engine.h"
#include "joseki/joseki_engine.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"

#ifdef DCNN
#include "dcnn/dcnn.h"
#include "dcnn/dcnn_engine.h"
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
cmd_gogui_analyze_commands(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "");  /* gtp prefix */

	if (e->best_moves) {
		printf("gfx/Best Moves/gogui-best_moves\n");
		printf("gfx/Best Winrates/gogui-winrates\n");
	}
	if (e->ownermap) {
		printf("gfx/Influence/gogui-influence\n");
		printf("gfx/Score Est/gogui-score_est\n");
	}
	if (e->dead_groups) {
		printf("gfx/Final Score/gogui-final_score\n");
		printf("plist/Dead Groups/final_status_list dead\n");
		//printf("plist/Final Status List Dead/final_status_list dead\n");
		//printf("plist/Final Status List Alive/final_status_list alive\n");
		//printf("plist/Final Status List Seki/final_status_list seki\n");
		//printf("plist/Final Status List Black/final_status_list black_territory\n");
		//printf("plist/Final Status List White/final_status_list white_territory\n");
	}
	if (!strcmp(e->name, "UCT") && using_joseki(b)) {
		printf("gfx/Joseki Moves/gogui-joseki_moves\n");
		printf("gfx/Joseki Range/gogui-joseki_show_pattern %%p\n");
	}
#ifdef DCNN                            /* board check fake since we're called once on startup ... */
	if (!strcmp(e->name, "UCT") && using_dcnn(b)) {
		printf("gfx/DCNN Best Moves/gogui-dcnn_best\n");
		printf("gfx/DCNN Color Map/gogui-dcnn_colors\n");
		printf("gfx/DCNN Ratings/gogui-dcnn_rating\n");
	}
#endif
	if (!strcmp(e->name, "UCT") && using_patterns()) {
		printf("gfx/Pattern Best Moves/gogui-pattern_best\n");
		printf("gfx/Pattern Color Map/gogui-pattern_colors\n");
		printf("gfx/Pattern Ratings/gogui-pattern_rating\n");
		printf("gfx/Pattern Features At/gogui-pattern_features %%p\n");
		printf("gfx/Pattern Gammas At/gogui-pattern_gammas %%p\n");
		printf("gfx/Set Spatial Size/gogui-spatial_size %%o\n");
		printf("gfx/Show Spatial/gogui-show_spatial %%p\n");
	}
	if (!strcmp(e->name, "UCT")) {
		printf("gfx/Live gfx = Best Moves/gogui-livegfx best_moves\n");
		printf("gfx/Live gfx = Best Sequence/gogui-livegfx best_seq\n");
		printf("gfx/Live gfx = Winrates/gogui-livegfx winrates\n");
		printf("gfx/Live gfx = None/gogui-livegfx\n");
	}
#ifdef JOSEKIFIX
	printf("gfx/          Josekifix Set Coord/gogui-josekifix_set_coord %%p\n");
	printf("gfx/          Josekifix Show Pattern/gogui-josekifix_show_pattern\n");
	printf("gfx/          Josekifix Dump Templates/gogui-josekifix_dump_templates\n");
#endif
	
	/* Debugging */
	if (DEBUGL(3))
		printf("gfx/Color Palette/gogui-color_palette\n");
	
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
gogui_paint_pattern(board_t *b, int colors[BOARD_MAX_COORDS][4],
		    coord_t coord, unsigned int maxd,
		    int rr, int gg, int bb)
{
	int cx = coord_x(coord), cy = coord_y(coord);
	for (unsigned int d = 2; d <= maxd; d++)
	for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, cx, cy, j);
		        coord_t c  = coord_xy(x, y);
			if (board_at(b, c) == S_OFFBOARD)  continue;
			
			/* Also show indices if debugging is on. */
			if (DEBUGL(3))  printf("LABEL %s %i\n", coord2sstr(c), j);

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
gogui_show_pattern(board_t *b, coord_t coord, int maxd)
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
		printf("COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c));
	} foreach_point_end;
}


/****************************************************************************************/

enum gogui_reporting gogui_livegfx = UR_GOGUI_NONE;

static void
gogui_set_livegfx(engine_t *e, board_t *b, char *arg)
{
	gogui_livegfx = UR_GOGUI_NONE;
	if (!strcmp(arg, "best_moves"))  gogui_livegfx = UR_GOGUI_BEST;
	if (!strcmp(arg, "best_seq"))    gogui_livegfx = UR_GOGUI_SEQ;
	if (!strcmp(arg, "winrates"))    gogui_livegfx = UR_GOGUI_WR;
	
	/* Override reportfreq to get decent update rates in GoGui */
	char *err;
	bool r = engine_setoptions(e, b, "reportfreq=0.2s", &err);  assert(r);
}

void
gogui_show_winrates(FILE *f, board_t *b, enum stone color, coord_t *best_c, float *best_r, int nbest)
{
	/* best move */
	if (best_c[0] != pass)
		fprintf(f, "VAR %s %s\n", (color == S_WHITE ? "w" : "b"), coord2sstr(best_c[0]) );
	
	for (int i = 0; i < nbest; i++)
		if (best_c[i] != pass)
			fprintf(f, "LABEL %s %i\n", coord2sstr(best_c[i]), (int)(roundf(best_r[i] * 100)));
}

void
gogui_show_best_seq(FILE *f, board_t *b, enum stone color, coord_t *seq, int n)
{	
	fprintf(f, "VAR ");
	for (int i = 0; i < n && seq[i] != pass; i++) {
		fprintf(f, "%.1s %3s ", stone2str(color), coord2sstr(seq[i]));
		color = stone_other(color);
	}
	fprintf(f, "\n");
}

/* Display best moves graphically in GoGui. */
void
gogui_show_best_moves(FILE *f, board_t *b, enum stone color, coord_t *best_c, float *best_r, int n)
{
        /* best move */
        if (best_c[0] != pass)
                fprintf(f, "VAR %.1s %s\n", stone2str(color), coord2sstr(best_c[0]));
        
        for (int i = 1; i < n; i++)
                if (best_c[i] != pass)
                        fprintf(f, "LABEL %s %i\n", coord2sstr(best_c[i]), i + 1);
}

/* Display best moves graphically in GoGui. */
static void
gogui_show_best_moves_colors(FILE *f, board_t *b, enum stone color,
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
		coord_t c = coord_xy(x, y);			
		int rr, gg, bb;
		value2color(vals[c], &rr, &gg, &bb);
		
		fprintf(f, "COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c));
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
gogui_best_moves(FILE *f, engine_t *e, board_t *b, time_info_t *ti,
		 enum stone color, int n, gogui_gfx_t gfx_type, gogui_rescale_t rescale)
{
	assert(color != S_NONE);
	time_info_t *ti_genmove = time_info_genmove(b, ti, color);
	
	coord_t best_c[n];
	float   best_r[n];
	engine_best_moves(e, b, ti_genmove, color, best_c, best_r, n);	
	rescale_best_moves(best_c, best_r, n, rescale);
	
	if      (gfx_type == GOGUI_BEST_WINRATES)  gogui_show_winrates(f, b, color, best_c, best_r, n);
	else if (gfx_type == GOGUI_BEST_MOVES)     gogui_show_best_moves(f, b, color, best_c, best_r, n);
	else if (gfx_type == GOGUI_BEST_COLORS)    gogui_show_best_moves_colors(f, b, color, best_c, best_r, n);
	else    assert(0);
}

enum parse_code
cmd_gogui_color_palette(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone color = board_to_play(b);
	float   best_r[GOGUI_MANY] = { 0.0, };
	coord_t best_c[GOGUI_MANY];
	for (int i = 0; i < GOGUI_MANY; i++)
		best_c[i] = coord_xy(i%19 +1, 18 - i/19 + 1);

	gtp_printf(gtp, "");  /* gtp prefix */
	rescale_best_moves(best_c, best_r, GOGUI_MANY, GOGUI_RESCALE_LINEAR);
	gogui_show_best_moves_colors(stdout, b, color, best_c, best_r, GOGUI_MANY);
	return P_OK;
}


enum parse_code
cmd_gogui_livegfx(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg_optional(arg);
	gogui_set_livegfx(e, b, arg);
	return P_OK;
}

enum parse_code
cmd_gogui_influence(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap)  {  gtp_error(gtp, "no ownermap");  return P_OK;  }
	
	gtp_printf(gtp, "INFLUENCE");
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
		printf(" %3s %.1lf", coord2sstr(c), p);
	} foreach_point_end;

	printf("\nTEXT Score Est: %s\n", ownermap_score_est_str(b, ownermap));
	return P_OK;
}

enum parse_code
cmd_gogui_score_est(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	ownermap_t *ownermap = engine_ownermap(e, b);
	if (!ownermap)  {  gtp_error(gtp, "no ownermap");  return P_OK;  }
	
	gtp_printf(gtp, "INFLUENCE");
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		enum point_judgement j = ownermap_score_est_coord(b, ownermap, c);
		float p = 0;
		if (j == PJ_BLACK)  p = 0.5;
		if (j == PJ_WHITE)  p = -0.5;
		printf(" %3s %.1lf", coord2sstr(c), p);
	} foreach_point_end;

	printf("\nTEXT Score Est: %s\n", ownermap_score_est_str(b, ownermap));
	return P_OK;
}

enum parse_code
cmd_gogui_final_score(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *msg = NULL;
	ownermap_t *o = engine_ownermap(e, b);
	if (o && !board_position_final(b, o, &msg)) {
		gtp_error(gtp, msg);
		return P_OK;
	}

	move_queue_t q;
	engine_dead_groups(e, b, &q);
	
	int dame, seki;
	int ownermap[board_max_coords(b)];
	floating_t score = board_official_score_details(b, &q, &dame, &seki, ownermap, NULL);
	
	gtp_printf(gtp, "INFLUENCE");
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		float p = 0;
		if (ownermap[c] == S_BLACK)  p = 0.5;
		if (ownermap[c] == S_WHITE)  p = -0.5;
		printf(" %3s %.1lf", coord2sstr(c), p);
	} foreach_point_end;
	printf("\n");
	
	if      (score == 0) printf("TEXT 0\n");
	else if (score > 0)  printf("TEXT W+%.1f\n", score);
	else                 printf("TEXT B+%.1f\n", -score);
	return P_OK;
}

enum parse_code
cmd_gogui_winrates(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */

	gogui_reporting_t prev = gogui_livegfx;
	gogui_set_livegfx(e, b, "winrates");
	gogui_best_moves(stdout, e, b, ti, color, GOGUI_MANY, GOGUI_BEST_WINRATES, GOGUI_RESCALE_NONE);
	gogui_livegfx = prev;
	
	return P_OK;
}

enum parse_code
cmd_gogui_best_moves(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */

	gogui_reporting_t prev = gogui_livegfx;
	gogui_set_livegfx(e, b, "best_moves");
	gogui_best_moves(stdout, e, b, ti, color, GOGUI_NBEST, GOGUI_BEST_MOVES, GOGUI_RESCALE_NONE);
	gogui_livegfx = prev;
	
	return P_OK;
}


#ifdef DCNN
/********************************************************************************************/
/* dcnn */

static engine_t *dcnn_engine = NULL;

enum parse_code
cmd_gogui_dcnn_best(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_error(gtp, "Not using dcnn");  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = new_engine(E_DCNN, "", b);
	
	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */	
	gogui_best_moves(stdout, dcnn_engine, b, ti, color, GOGUI_NBEST, GOGUI_BEST_MOVES, GOGUI_RESCALE_NONE);
	return P_OK;
}

enum parse_code
cmd_gogui_dcnn_colors(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_error(gtp, "Not using dcnn");  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = new_engine(E_DCNN, "", b);
	
	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");  /* gtp prefix */
	gogui_best_moves(stdout, dcnn_engine, b, ti, color, GOGUI_MANY, GOGUI_BEST_COLORS, GOGUI_RESCALE_LOG);
	return P_OK;
}

enum parse_code
cmd_gogui_dcnn_rating(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!using_dcnn(b)) {  gtp_error(gtp, "Not using dcnn");  return P_OK;  }
	if (!dcnn_engine)   dcnn_engine = new_engine(E_DCNN, "", b);

	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */
	gogui_best_moves(stdout, dcnn_engine, b, ti, color, GOGUI_MANY, GOGUI_BEST_WINRATES, GOGUI_RESCALE_NONE);
	return P_OK;
}

#endif /* DCNN */


/********************************************************************************************/
/* joseki */

static engine_t *joseki_engine = NULL;

enum parse_code
cmd_gogui_joseki_moves(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!using_joseki(b)) {  gtp_reply(gtp, "TEXT Not using joseki");  return P_OK;  }
	if (!joseki_engine)   joseki_engine = new_engine(E_JOSEKI, NULL, b);

	enum stone color = board_to_play(b);
	float joseki_map[BOARD_MAX_COORDS];
	joseki_rate_moves(joseki_dict, b, color, joseki_map);

	gtp_printf(gtp, "");   /* gtp prefix */

	/* Show relaxed / ignored moves */
	foreach_free_point(b) {
		josekipat_t *p = joseki_lookup_ignored(joseki_dict, b, c, color);
		if (p)  printf("MARK %s\n", coord2sstr(c));
		if (p && (p->flags & JOSEKI_FLAGS_3X3))
			printf("CIRCLE %s\n", coord2sstr(c));
		
		p = joseki_lookup_3x3(joseki_dict, b, c, color);
		if (p)  printf("CIRCLE %s\n", coord2sstr(c));
	} foreach_free_point_end;

	gogui_best_moves(stdout, joseki_engine, b, ti, color, GOGUI_MANY, GOGUI_BEST_COLORS, GOGUI_RESCALE_LOG);

	/* Show ignored moves, background color */
	foreach_free_point(b) {
		if (joseki_map[c]) continue;  /* Don't clobber valid moves ! */
		josekipat_t *p = joseki_lookup_ignored(joseki_dict, b, c, color);
		if (!p)  continue;
		printf("COLOR #0000a0 %s\n", coord2sstr(c));
	} foreach_free_point_end;

	return P_OK;
}

enum parse_code
cmd_gogui_joseki_show_pattern(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;  gtp_arg(arg);
	coord_t coord = str2coord(arg);

	gtp_printf(gtp, "");  /* gtp prefix */
	gogui_show_pattern(b, coord, JOSEKI_PATTERN_DIST);
	return P_OK;
}


/********************************************************************************************/
/* josekifix */

#ifdef JOSEKIFIX

static void
dump_template_entry_full(char *prefix, struct board *b, coord_t at, unsigned int d)
{
	enum stone color = last_move(b).color;
	//board_print_pattern_full(b, at, d);
	
	/* Normal case ... */
	if (at == last_move(b).coord) {
		printf("%s{ \"%s\", \"XXX\", \"\", { ", prefix, coord2sstr(last_move(b).coord));
		goto dump_hash;
	}

	/* Match at given coord ... */
	char *field = ".coord_empty";
	if (board_at(b, at) == color)               field = ".coord_other";
	if (board_at(b, at) == stone_other(color))  field = ".coord_own";
	printf("%s{ %s = \"%s\", .prev = \"%s\", \"XXX\", \"\", \n", prefix,
		 field, coord2sstr(at), coord2sstr(last_move(b).coord));
	printf("%s                   { ", prefix);

 dump_hash:
	for (int rot = 0; rot < 8; rot++) {
		hash_t h = outer_spatial_hash_from_board_rot_d(b, at, color, rot, d);
		printf("0x%"PRIhash"%s ", h, (rot != 7 ? "," : ""));
		if (rot == 3)  printf("\n%s                     ", prefix);
	}
	printf("} },\n%s\n", prefix);
}

/* Dump template entry for position */
static void
dump_template_entry(char *prefix, struct board *b, coord_t at)
{
	dump_template_entry_full(prefix, b, at, MAX_PATTERN_DIST);
}

static bool dump_templates = false;
static coord_t dump_patterns_coord = pass;

//static bool josekifix_get_dump_templates()         {  return dump_templates;  }
static void josekifix_set_dump_templates(bool val) {  dump_templates = val;   }
static void josekifix_set_coord(coord_t c)         {  dump_patterns_coord = c;  }

static void
josekifix_paint_pattern_full(struct board *b, int colors[BOARD_MAX_COORDS][4],
			     coord_t coord, unsigned int maxd,
			     int rr, int gg, int bb)
{
	int cx = coord_x(coord);    int cy = coord_y(coord);
	
	for (unsigned int d = 2; d <= maxd; d++)
	for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
		ptcoords_at(x, y, cx, cy, j);
		coord_t c  = coord_xy(x, y);
		if (board_at(b, c) == S_OFFBOARD)  continue;
		
/* Just lighten if already something */
#define add_primary_color(p, val)  colors[c][p] = (val) + (colors[c][p] ? 30 : 0);

		add_primary_color(0, rr);
		add_primary_color(1, gg);
		add_primary_color(2, bb);
		colors[c][3]++; /* count */
	}
}

static void
josekifix_paint_pattern(struct board *b, int colors[BOARD_MAX_COORDS][4],
			coord_t coord, int rr, int gg, int bb)
{
	josekifix_paint_pattern_full(b, colors, coord, MAX_PATTERN_DIST, rr, gg, bb);
}

static void
josekifix_gogui_show_patterns(struct board *b)
{
	int colors[BOARD_MAX_COORDS][4];  memset(colors, 0, sizeof(colors));
	if (is_pass(dump_patterns_coord))
		dump_patterns_coord = str2coord("E15");
	
	josekifix_paint_pattern(b, colors, last_move(b).coord, 0x00, 0x8a, 0xff);
	josekifix_paint_pattern(b, colors, dump_patterns_coord, 0xff, 0xa2, 0x00);

	if (dump_templates) {
		dump_template_entry("TEXT ", b, dump_patterns_coord);
		dump_template_entry("TEXT ", b, last_move(b).coord);
	}
	
	foreach_point(b) {
		if (!colors[c][3]) continue;
		int rr = MIN(colors[c][0], 255);
		int gg = MIN(colors[c][1], 255);
		int bb = MIN(colors[c][2], 255);
		printf("COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c));
	} foreach_point_end;
}

enum parse_code
cmd_gogui_josekifix_set_coord(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;  gtp_arg(arg);
	coord_t coord = str2coord(arg);
	josekifix_set_coord(coord);
	return cmd_gogui_josekifix_show_pattern(b, e, ti, gtp);
}

enum parse_code
cmd_gogui_josekifix_show_pattern(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "");
	josekifix_gogui_show_patterns(b);
	return P_OK;
}

enum parse_code
cmd_gogui_josekifix_dump_templates(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	josekifix_set_dump_templates(true);
	cmd_gogui_josekifix_show_pattern(b, e, ti, gtp);
	josekifix_set_dump_templates(false);
	return P_OK;
}

#endif


/********************************************************************************************/
/* pattern */

static engine_t *pattern_engine = NULL;

static void
init_pattern_engine(board_t *b)
{
	char args[] = "mcowner_fast=0";
	pattern_engine = new_engine(E_PATTERN, args, b);
}

enum parse_code
cmd_gogui_pattern_best(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_pattern_engine(b);

	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */
	gogui_best_moves(stdout, pattern_engine, b, ti, color, GOGUI_NBEST, GOGUI_BEST_MOVES, GOGUI_RESCALE_NONE);

	bool locally = pattern_engine_matched_locally(pattern_engine);
	printf("TEXT Matching Locally: %s\n", (locally ? "Yes" : "No"));
	return P_OK;
}

enum parse_code
cmd_gogui_pattern_colors(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_pattern_engine(b);

	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");  /* gtp prefix */	
	gogui_best_moves(stdout, pattern_engine, b, ti, color, GOGUI_MANY, GOGUI_BEST_COLORS, GOGUI_RESCALE_LOG);

	bool locally = pattern_engine_matched_locally(pattern_engine);
	printf("TEXT Matching Locally: %s\n", (locally ? "Yes" : "No"));
	return P_OK;
}

enum parse_code
cmd_gogui_pattern_rating(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_pattern_engine(b);

	enum stone color = board_to_play(b);	

	gtp_printf(gtp, "");   /* gtp prefix */
	gogui_best_moves(stdout, pattern_engine, b, ti, color, GOGUI_MANY, GOGUI_BEST_WINRATES, GOGUI_RESCALE_NONE);

	bool locally = pattern_engine_matched_locally(pattern_engine);
	printf("TEXT Matching Locally: %s\n", (locally ? "Yes" : "No"));
	return P_OK;
}

/* Show pattern features on point selected by user. */
enum parse_code
cmd_gogui_pattern_features(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone color = board_to_play(b);
	
	char *arg;  gtp_arg(arg);
	coord_t coord = str2coord(arg);
	if (board_at(b, coord) != S_NONE)  {  gtp_reply(gtp, "TEXT Must be empty spot ...");  return P_OK;  }
	
	pattern_t p;
	move_t m = move(coord, color);
	pattern_context_t *ct = pattern_context_new(b, color, false);
	bool locally = pattern_matching_locally(b, color, ct);
	pattern_match(b, &m, &p, ct, locally);
	pattern_context_free(ct);

	/* Show largest spatial */
	int dist = 0;
	for (int i = 0; i < p.n; i++)
		if (p.f[i].id >= FEAT_SPATIAL3)
			dist = MAX(dist, (int)p.f[i].id - FEAT_SPATIAL3 + 3);
	
	gtp_printf(gtp, "TEXT %s\n", pattern2sstr(&p));
	if (dist)  gogui_show_pattern(b, coord, dist);
	
	return P_OK;
}

/* Show pattern gammas on point selected by user. */
enum parse_code
cmd_gogui_pattern_gammas(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone color = board_to_play(b);	
	char *arg;  gtp_arg(arg);
	coord_t coord = str2coord(arg);
	if (board_at(b, coord) != S_NONE)  {  gtp_reply(gtp, "TEXT Must be empty spot ...");  return P_OK;  }

	pattern_t p;
	move_t m = move(coord, color);
	pattern_context_t *ct = pattern_context_new(b, color, false);
	bool locally = pattern_matching_locally(b, color, ct);
	pattern_match(b, &m, &p, ct, locally);
	
	strbuf(buf, 1000);
	dump_gammas(buf, &p);
	pattern_context_free(ct);

	gtp_printf(gtp, "TEXT %s\n", buf->str);
	return P_OK;
}

static int spatial_dist = 6;

enum parse_code
cmd_gogui_show_spatial(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!pattern_engine)   init_pattern_engine(b);
	pattern_config_t *pc = pattern_engine_get_pc(pattern_engine);

	char *arg;  gtp_arg(arg);
	coord_t coord = str2coord(arg);

	gtp_printf(gtp, "");   /* gtp prefix */
	gogui_show_pattern(b, coord, spatial_dist);
	
	move_t m = move(coord, board_to_play(b));
	spatial_t s;
	spatial_from_board(pc, &s, b, &m);
	s.dist = spatial_dist;
	spatial_t *s2 = spatial_dict_lookup(s.dist, spatial_hash(0, &s));
	if (s2)	printf("TEXT matches s%i:%i\n", spatial_dist, spatial_id(s2));
	else	printf("TEXT unknown s%i spatial\n", spatial_dist);

	spatial_write(&s, 0, stderr);

	return P_OK;
}

enum parse_code
cmd_gogui_spatial_size(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;  gtp_arg_optional(arg);
	/* Return current value */
	if (!*arg) {  gtp_printf(gtp, "%i\n", spatial_dist);  return P_OK;  }

	int d = atoi(arg);
	if (d < 3 || d > 10) {  gtp_error(gtp, "Between 3 and 10 please");  return P_OK;  }
	spatial_dist = d;
	return P_OK;
}
