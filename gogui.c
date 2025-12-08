#define DEBUG
#include <assert.h>
#include <math.h>
#include "board.h"
#include "engine.h"
#include "timeinfo.h"
#include "gtp.h"
#include "gogui.h"
#include "ownermap.h"
#include "joseki/joseki.h"
#include "uct/uct.h"
#include "pattern/pattern.h"
#include "pattern/pattern_engine.h"
#include "joseki/joseki_engine.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"
#include "josekifix/override.h"
#include "tactics/selfatari.h"

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

#define GOGUI_VERSION(major, minor, patch)	(((major) << 16) | ((minor) << 8) | (patch))
#define GOGUI_VERSION_MAJOR(version)		(((version) & 0xff0000) >> 16)
#define GOGUI_VERSION_MINOR(version)		(((version) & 0xff00) >> 8)
#define GOGUI_VERSION_PATCH(version)		((version) & 0xff)

/* Version of GoGui we're running in (0 if unknown). */
static int gogui_version = 0;

/* GoGui >= 1.4.12 declares its version with gogui-version command. */
enum parse_code
cmd_gogui_version(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	char *arg;
	gtp_arg(arg);

	/* Parse gogui version */
	if (!isdigit(*arg))  {  gtp_error(gtp, "invalid version");  return P_OK;  }
	int major = atoi(arg);
	while (isdigit(*arg))
		arg++;
	if (*arg++ != '.')  {  gtp_error(gtp, "invalid version");  return P_OK;  }

	if (!isdigit(*arg))  {  gtp_error(gtp, "invalid version");  return P_OK;  }
	int minor = atoi(arg);
	while (isdigit(*arg))
		arg++;
	if (*arg++ != '.')  {  gtp_error(gtp, "invalid version");  return P_OK;  }

	if (!isdigit(*arg))  {  gtp_error(gtp, "invalid version");  return P_OK;  }
	int patch = atoi(arg);

	int v = gogui_version = GOGUI_VERSION(major, minor, patch);

	if (DEBUGL(3))  fprintf(stderr, "Running in gogui version %i.%i.%i\n", GOGUI_VERSION_MAJOR(v), GOGUI_VERSION_MINOR(v), GOGUI_VERSION_PATCH(v));

	return P_OK;
}

/* Show debugging analyze commands ? */
static int debugging_commands = -1;

/* Toggle show/hide debugging analyze commands */
enum parse_code
cmd_gogui_toggle_debugging_commands(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	debugging_commands = !debugging_commands;
	return P_OK;
}

enum parse_code
cmd_gogui_analyze_commands(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	gtp_printf(gtp, "");  /* gtp prefix */

	if (e->best_moves) {
		printf("gfx/Best Moves/gogui-best_moves/Show engine best moves\n");
		printf("gfx/Best Winrates/gogui-winrates/Show best moves' winrates\n");
	}
	if (e->ownermap) {
		printf("gfx/Influence/gogui-influence/Show black and white areas of influence\n");
		printf("gfx/Score Estimate/gogui-score_est/Score estimate\n");
	}
	if (e->dead_groups) {
		printf("gfx/Final Score/gogui-final_score/Official score\n");
		printf("plist/Dead Stones/final_status_list dead/Show dead stones\n");
		//printf("plist/Final Status List Alive/final_status_list alive\n");
		//printf("plist/Final Status List Seki/final_status_list seki\n");
		//printf("plist/Final Status List Black/final_status_list black_territory\n");
		//printf("plist/Final Status List White/final_status_list white_territory\n");
	}
	if (str_prefix("UCT", e->name) && using_joseki(b)) {
		printf("gfx/Joseki Moves/gogui-joseki_moves/Show engine's joseki moves\n");
		printf("gfx/Joseki Range/gogui-joseki_show_pattern %%p/Show joseki spatial pattern at selected coordinate\n");
	}
#ifdef DCNN                            /* board check fake since we're called once on startup ... */
	if (str_prefix("UCT", e->name) && using_dcnn(b)) {
		printf("gfx/DCNN Best Moves/gogui-dcnn_best/Show neural network best moves\n");
		printf("gfx/DCNN Color Map/gogui-dcnn_colors/Show neural network ratings (red=best)\n");
		printf("gfx/DCNN Ratings/gogui-dcnn_rating/Show neural network ratings\n");
	}
#endif
	if (str_prefix("UCT", e->name) && using_patterns()) {
		printf("gfx/Pattern Best Moves/gogui-pattern_best/Show pattern best moves\n");
		printf("gfx/Pattern Color Map/gogui-pattern_colors/Show pattern ratings (red=best)\n");
		printf("gfx/Pattern Ratings/gogui-pattern_rating/Show pattern ratings\n");
		printf("gfx/Pattern Features At/gogui-pattern_features %%p/Show pattern features at selected coordinate\n");
		printf("gfx/Pattern Gammas At/gogui-pattern_gammas %%p/Show pattern features and gammas at selected coordinate\n");
		printf("gfx/Set Spatial Size/gogui-spatial_size %%o/Set spatial pattern size for Show Spatial command\n");
		printf("gfx/Show Spatial/gogui-show_spatial %%p/Show spatial pattern at selected coordinate\n");
	}
	if (str_prefix("UCT", e->name)) {
		printf("gfx/Playout Moves/gogui-playout_moves/Show playout most played moves for current position\n");
		printf("gfx/Live gfx = Best Moves/gogui-livegfx best_moves/Show best moves while engine is thinking\n");
		printf("gfx/Live gfx = Best Sequence/gogui-livegfx best_seq/Show best sequence while engine is thinking\n");
		printf("gfx/Live gfx = Winrates/gogui-livegfx winrates/Show best moves' winrates while engine is thinking\n");
		printf("gfx/Live gfx = None/gogui-livegfx/Don't display anything while engine is thinking\n");
	}

	/* Show debugging commands by default ? */
	if (debugging_commands == -1)
		debugging_commands = DEBUGL(3);

	/* Debugging commands:
	 * Can toggle from analyze window (gogui >= 1.4.12) */
	bool can_toggle = (gogui_version >= GOGUI_VERSION(1, 4, 12) && gogui_version < GOGUI_VERSION(1, 5, 0));
	printf("gfx/ /echo\n");
	if (!debugging_commands) {
		if (can_toggle)  printf("reload/[ Debugging ]/gogui-toggle_debugging_commands/Double click to show debugging commands\n");
	} else {
		if (can_toggle)  printf("reload/[ Debugging ]/gogui-toggle_debugging_commands/Debugging commands. Double click to hide\n");
		else             printf("gfx/[ Debugging ]/echo\n");
#ifdef JOSEKIFIX
		printf("gfx/Josekifix Show Pattern/gogui-josekifix_show_pattern %%p/Show josekifix spatial pattern around selected coordinate and last move\n");
		printf("gfx/Josekifix Dump Templates/gogui-josekifix_dump_templates %%p/Make josekifix override templates for selected coordinate and last move\n");
#endif
		printf("gfx/Bad Selfatari/gogui-bad_selfatari/Show selfataris (green=good, red=bad)\n");
		printf("gfx/Color Palette/gogui-color_palette/Show color palette used by colormap functions\n");
	}

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
gogui_show_winrates(FILE *f, board_t *b, enum stone color, best_moves_t *best)
{
	/* best move */
	if (best->n && !is_pass(best->c[0]))
		fprintf(f, "VAR %s %s\n", (color == S_WHITE ? "w" : "b"), coord2sstr(best->c[0]) );

	for (int i = 0; i < best->n; i++)
		if (!is_pass(best->c[i]))
			fprintf(f, "LABEL %s %i\n", coord2sstr(best->c[i]), (int)(roundf(best->r[i] * 100)));
}

void
gogui_show_best_seq(FILE *f, board_t *b, enum stone color, mq_t *seq)
{
	/* Can have passes in the sequence, gogui handles it. */
	fprintf(f, "VAR ");
	for (int i = 0; i < seq->moves; i++) {
		fprintf(f, "%.1s %3s ", stone2str(color), coord2sstr(seq->move[i]));
		color = stone_other(color);
	}
	fprintf(f, "\n");
}

/* Display best moves graphically in GoGui. */
void
gogui_show_best_moves(FILE *f, board_t *b, enum stone color, best_moves_t *best)
{
        /* best move */
        if (best->n && !is_pass(best->c[0]))
                fprintf(f, "VAR %.1s %s\n", stone2str(color), coord2sstr(best->c[0]));
        
        for (int i = 1; i < best->n; i++)
		if (!is_pass(best->c[i]))
			fprintf(f, "LABEL %s %i\n", coord2sstr(best->c[i]), i + 1);
}

/* Display best moves graphically in GoGui. */
static void
gogui_show_best_moves_colors(FILE *f, board_t *b, enum stone color, best_moves_t *best)
{
	float vals[BOARD_MAX_COORDS] = { 0, };
	
	for (int i = 0; i < best->n; i++)
		if (!is_pass(best->c[i]))
			vals[best->c[i]] = best->r[i];

	foreach_point_for_print(b) {
		int rr, gg, bb;
		value2color(vals[c], &rr, &gg, &bb);

		fprintf(f, "COLOR #%02x%02x%02x %s\n", rr, gg, bb, coord2sstr(c));
	} foreach_point_for_print_end;
}

static void
rescale_best_moves(best_moves_t *best, int rescale)
{
	int n = best->n;
	
	if (rescale & GOGUI_RESCALE_LINEAR) {
		for (int i = 0; i < n; i++) {
			best->r[i] = (float)(n-i)/n;
			//fprintf(stderr, "linear: %i\n", (int)(best_r[i] * 100));
		}
	}       

	if (rescale & GOGUI_RESCALE_LOG) {
		float max = log(1.0 * 1000);
		for (int i = 0; i < n; i++) {
			best->r[i] = log(best->r[i] * 1000) / max;
			if (best->r[i] < 0)  best->r[i] = 0.;
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
	best_moves_setup(best, best_c, best_r, n);

	engine_best_moves(e, b, ti_genmove, color, &best);
	rescale_best_moves(&best, rescale);
	
	if      (gfx_type == GOGUI_BEST_WINRATES)  gogui_show_winrates(f, b, color, &best);
	else if (gfx_type == GOGUI_BEST_MOVES)     gogui_show_best_moves(f, b, color, &best);
	else if (gfx_type == GOGUI_BEST_COLORS)    gogui_show_best_moves_colors(f, b, color, &best);
	else    assert(0);
}

enum parse_code
cmd_gogui_color_palette(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	int size = board_rsize(b);
	enum stone color = board_to_play(b);
	float   best_r[GOGUI_MANY];
	coord_t best_c[GOGUI_MANY];
	best_moves_setup(best, best_c, best_r, GOGUI_MANY);
	
	for (int i = 0; i < GOGUI_MANY; i++)
		best_c[i] = coord_xy(i%size +1, size-1 - i/size + 1);

	gtp_printf(gtp, "");  /* gtp prefix */
	rescale_best_moves(&best, GOGUI_RESCALE_LINEAR);
	gogui_show_best_moves_colors(stdout, b, color, &best);
	return P_OK;
}

/* Show all good/bad selfataris for color to play */
enum parse_code
cmd_gogui_bad_selfatari(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	enum stone to_play = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */
	
	mq_t q;  mq_init(&q);
	foreach_free_point(b) {
		if (!is_selfatari(b, to_play, c))
			continue;

		char *color = "#00ff00";  // green
		if (is_bad_selfatari(b, to_play, c)) {
			color = "#ff0000";  // red
			mq_add(&q, c);
		}
		printf("COLOR %s %s\n", color, coord2sstr(c));
	} foreach_free_point_end;

	printf("TEXT %s ", stone2str(to_play));
	mq_sort(&q);
	mq_print_file(&q, stdout, "");
	printf("\n");

	return P_OK;
}

static engine_t *playout_engine = NULL;

static void
init_playout_engine(board_t *b)
{
	char args[] = "runs=1000";
	playout_engine = new_engine(E_REPLAY, args, b);
}

enum parse_code
cmd_gogui_playout_moves(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	if (!playout_engine)   init_playout_engine(b);

	enum stone color = board_to_play(b);

	gtp_printf(gtp, "");   /* gtp prefix */
	gogui_best_moves(stdout, playout_engine, b, ti, color, GOGUI_MANY, GOGUI_BEST_WINRATES, GOGUI_RESCALE_NONE);
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

	mq_t q;
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
	coord_t coord;
	gtp_arg_coord(coord);

	gtp_printf(gtp, "");  /* gtp prefix */
	gogui_show_pattern(b, coord, JOSEKI_PATTERN_DIST);
	return P_OK;
}


/********************************************************************************************/
/* josekifix */

#ifdef JOSEKIFIX

static void
dump_template_entry_full(char *prefix, struct board *b, coord_t at)
{
	enum stone color = last_move(b).color;
	//board_print_pattern_full(b, at, d);
	
	/* Normal case ... */
	if (at == last_move(b).coord) {
		printf("%s{ \"%s\", \"XXX\", \"\", { ", prefix, coord2sstr(last_move(b).coord));
		goto dump_hash;
	}

	/* Match at given coord ... */
	printf("%s{ .coord = \"%s\", .prev = \"%s\", \"XXX\", \"\", \n", prefix,
		 coord2sstr(at), coord2sstr(last_move(b).coord));
	printf("%s                   { ", prefix);

 dump_hash:
	for (int rot = 0; rot < 8; rot++) {
		hash_t h = josekifix_spatial_hash_rot(b, at, color, rot);
		printf("0x%"PRIhash"%s ", h, (rot != 7 ? "," : ""));
		if (rot == 3)  printf("\n%s                     ", prefix);
	}
	printf("} },\n%s\n", prefix);
}

/* Dump template entry for position */
static void
dump_template_entry(char *prefix, struct board *b, coord_t at)
{
	dump_template_entry_full(prefix, b, at);
}

static void
josekifix_paint_pattern(struct board *b, int colors[BOARD_MAX_COORDS][4],
			coord_t coord, int rr, int gg, int bb)
{
	int cx = coord_x(coord);    int cy = coord_y(coord);
	
	for (unsigned int d = 2; d <= MAX_PATTERN_DIST; d++)
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
josekifix_show_pattern(struct board *b, gtp_t *gtp, coord_t at, bool dump_templates)
{
	gtp_printf(gtp, "");
	
	int colors[BOARD_MAX_COORDS][4];  memset(colors, 0, sizeof(colors));
	assert(!is_pass(at));

	josekifix_paint_pattern(b, colors, last_move(b).coord, 0x00, 0x8a, 0xff);
	josekifix_paint_pattern(b, colors, at, 0xff, 0xa2, 0x00);

	if (dump_templates) {
		dump_template_entry("TEXT ", b, at);
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
cmd_gogui_josekifix_show_pattern(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	coord_t coord;
	gtp_arg_coord(coord);
	
	josekifix_show_pattern(b, gtp, coord, false);
	return P_OK;
}

enum parse_code
cmd_gogui_josekifix_dump_templates(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	coord_t coord;
	gtp_arg_coord(coord);

	josekifix_show_pattern(b, gtp, coord, true);
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
	coord_t coord;
	gtp_arg_coord(coord);

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
	coord_t coord;
	gtp_arg_coord(coord);
	
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

	coord_t coord;
	gtp_arg_coord(coord);

	gtp_printf(gtp, "");   /* gtp prefix */
	gogui_show_pattern(b, coord, spatial_dist);
	
	move_t m = move(coord, board_to_play(b));
	spatial_t s;
	spatial_from_board(pc, &s, b, &m);
	s.dist = spatial_dist;
	spatial_t *s2 = spatial_dict_lookup(s.dist, spatial_hash(0, &s));
	if (s2)	printf("TEXT matches s%i:%i\n", spatial_dist, spatial_payload(s2));
	else	printf("TEXT unknown s%i spatial\n", spatial_dist);

	spatial_write(&s, 0, stderr);

	return P_OK;
}

enum parse_code
cmd_gogui_spatial_size(board_t *b, engine_t *e, time_info_t *ti, gtp_t *gtp)
{
	/* No argument: Return current value */
	if (!*gtp->next) {
		gtp_printf(gtp, "%i\n", spatial_dist);
		return P_OK;
	}

	int d;
	gtp_arg_number(d);
	if (d < 3 || d > 10) {  gtp_error(gtp, "Between 3 and 10 please");  return P_OK;  }
	spatial_dist = d;
	return P_OK;
}
