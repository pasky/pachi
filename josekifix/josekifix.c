#include <assert.h>
#include <stdarg.h>
#include <math.h>

#define DEBUG
#include "board.h"
#include "random.h"
#include "pattern/spatial.h"
#include "ownermap.h"
#include "engine.h"
#include "dcnn.h"
#include "joseki/joseki.h"
#include "josekifix/josekifix.h"
#include "tactics/util.h"
#include "tactics/2lib.h"
#include "tactics/ladder.h"
#include "pachi.h"
#include "engines/external.h"

coord_t joseki_override_(struct board *b, strbuf_t *log,
			 struct ownermap *prev_ownermap, struct ownermap *ownermap,
			 bool external_engine_enabled);


static bool josekifix_enabled = true;
static bool josekifix_required = false;
void disable_josekifix()  {  josekifix_enabled = false;  }
void require_josekifix()  {  josekifix_required = true;  }


static override_t *joseki_overrides = NULL;
static override_t *logged_variations = NULL;


/*****************************************************************************/
/* External engine */

char     *external_joseki_engine_cmd = "katago gtp";
engine_t *external_joseki_engine = NULL;
int       external_joseki_engine_genmoved = 0;

static bool want_external_engine_next = false;
static bool want_external_engine_diag_next = false;
static bool external_engine_overrides_enabled = true;

static void
external_joseki_engine_init(board_t *b)
{
	char *cmd = external_joseki_engine_cmd;	
	if (!cmd)  return;

	strbuf(buf, 1024);
	strbuf_printf(buf, "cmd=%s", cmd);
	external_joseki_engine = new_engine(E_EXTERNAL, buf->str, b);
	if (!external_engine_started(external_joseki_engine))
		external_joseki_engine = NULL;
}

void
external_joseki_engine_play(coord_t c, enum stone color)
{
	if (!external_joseki_engine)  return;
	
	strbuf(buf, 100);
	strbuf_printf(buf, "play %s %s", stone2str(color), coord2sstr(c));
	char *reply, *error;
	int r = external_engine_send_cmd(external_joseki_engine, buf->str, &reply, &error);
	assert(r);	
}

void
external_joseki_engine_fixed_handicap(int stones)
{
	if (!external_joseki_engine)  return;
	
	strbuf(buf, 100);
	strbuf_printf(buf, "fixed_handicap %i", stones);
	char *reply, *error;
	int r = external_engine_send_cmd(external_joseki_engine, buf->str, &reply, &error);
	assert(r);
}

static void
external_joseki_engine_undo(board_t *b)
{
	if (DEBUGL(3))  fprintf(stderr, "external joseki engine undo\n");
	char *reply, *error;
	int r = external_engine_send_cmd(external_joseki_engine, "undo", &reply, &error);
	if (!r)  fprintf(stderr, "external joseki engine undo failed !\n");
}

static coord_t
external_joseki_engine_genmove(board_t *b)
{
	if (!external_joseki_engine)  return pass;

	char* cmd = (board_to_play(b) == S_BLACK ? "genmove b" : "genmove w");
	char *reply, *error;
	double time_start = time_now();
	int r = external_engine_send_cmd(external_joseki_engine, cmd, &reply, &error);
	if (!r) {
		fprintf(stderr, "external joseki engine genmove failed !\n");
		return pass;
	}

	external_joseki_engine_genmoved = 1;
    
	coord_t c = str2coord(reply);
	if (DEBUGL(2))  fprintf(stderr, "external joseki engine move: %s  (%.1fs)\n", coord2sstr(c), time_now() - time_start);
	
	return c;
}

static char* forwarded_external_engine_commands[] =
{
	"boardsize",
	"clear_board",
	"komi",
	"play",
	//"genmove",		// special handling
	"set_free_handicap",
	//"place_free_handicap",  // special handling
	"fixed_handicap",
	"undo",
	//"kgs-genmove_cleanup",	// special handling
	NULL
};

/* Forward gtp command (if needed) to external engine. */
void
external_joseki_engine_forward_cmd(gtp_t *gtp, char *command)
{
	if (!external_joseki_engine)  return;
    
	char** commands = forwarded_external_engine_commands;
	for (int i = 0; commands[i]; i++)
		if (!strcasecmp(gtp->cmd, commands[i])) {
			char *reply, *error;
			int r = external_engine_send_cmd(external_joseki_engine, command, &reply, &error);
			if (!r)  fprintf(stderr, "external engine: cmd '%s' failed: %s\n", gtp->cmd, error);
			break;
		}
}

/* <external joseki engine mode> on in this quadrant for next 15 moves */
static void
set_external_engine_mode_on(board_t *b, int quad)
{
	if (quad != -1)
		b->external_joseki_engine_moves_left_by_quadrant[quad] = 15;
}

/* If last move near middle, turn on adjacent quadrant as well */
static void
check_set_external_engine_mode_adjacent(board_t *b)
{	
	int x = coord_x(last_move(b).coord);
	int y = coord_y(last_move(b).coord);
	int mid = (board_rsize(b) + 1) / 2;
	int adx = abs(mid - x);
	int ady = abs(mid - y);
	
	if (adx < ady && adx <= 2) {
	    if (y > mid) {
		set_external_engine_mode_on(b, 0);
		set_external_engine_mode_on(b, 1);
	    }
	    if (y < mid) {
		set_external_engine_mode_on(b, 2);
		set_external_engine_mode_on(b, 3);
	    }	    
	}
	
	if (ady < adx && ady <= 2) {
	    if (x < mid) {
		set_external_engine_mode_on(b, 0);
		set_external_engine_mode_on(b, 3);
	    }	    
	    if (x > mid) {
		set_external_engine_mode_on(b, 1);
		set_external_engine_mode_on(b, 2);
	    }
	}
}

#if 0
/* <external joseki engine mode> on in all quadrannts */
static void
set_external_engine_mode_all_quadrants(board_t *b)
{
	for (int quad = 0; quad < 4; quad++)
		set_external_engine_mode_on(b, quad);
}
#endif


/*****************************************************************************/
/* Logging */

static strbuf_t *log_buf = NULL;

/* Everything here called from joseki_override() should use this for logging.
 * (let caller control logging) */
void
josekifix_log(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	if (log_buf)
		strbuf_vprintf(log_buf, format, ap);
	va_end(ap);    
}


/*****************************************************************************/
/* board_print_pattern(): visualize pattern area */

/*
typedef struct {
	coord_t coord;
	unsigned int d;
} t_print_pattern_data;

static char*
print_pattern_handler(struct board *b, coord_t c, void *pdata)
{
	t_print_pattern_data *data = pdata;
	int cx = coord_x(data->coord);    int cy = coord_y(data->coord);
	
	for (unsigned int d = 2; d <= data->d; d++)
	for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
		ptcoords_at(x, y, cx, cy, j);
		if (coord_xy(b, x, y) == c)
			return "*";
	}
	
	static char s[2] = { 0, };
	s[0] = stone2char(board_at(b, c));
	return s;
}

static void
board_print_pattern_full(struct board *b, coord_t coord, unsigned int d)
{
	t_print_pattern_data data = { .coord = coord, .d = d };
	board_hprint(b, stderr, print_pattern_handler, (void*)&data);
}

static void
board_print_pattern(struct board *b, coord_t coord)
{
	board_print_pattern_full(b, coord, MAX_PATTERN_DIST);
}
*/


/*****************************************************************************/
/* Ladder checks */

/* Add ladder check setup stones */
static bool
josekifix_ladder_setup(board_t *b, int rot, ladder_check_t *check)
{
	enum stone color = board_to_play(b);
	int n = JOSEKIFIX_LADDER_SETUP_MAX;
	
	for (int i = 0; i < n; i++) {
		char *coordstr = check->setup_own[i];
		if (!coordstr)  break;
		coord_t c = str2coord(coordstr);
		c = rotate_coord(c, rot);
		move_t m = { c, color };
		//josekifix_log("setup %s %s\n", stone2str(color), coord2str(c));
		int r = board_play(b, &m);
		if (r != 0)  return false;		// shouldn't happen really
	}
	
	for (int i = 0; i < n; i++) {
		char *coordstr = check->setup_other[i];
		if (!coordstr)  break;
		coord_t c = str2coord(coordstr);
		c = rotate_coord(c, rot);
		move_t m = { c, stone_other(color) };
		//josekifix_log("setup %s %s\n", stone2str(stone_other(color)), coord2str(c));
		int r = board_play(b, &m);
		if (r != 0)  return false;		// shouldn't happen really
	}

	return true;
}

/* Check override ladder check matches.
 * note: we assume alternating colors, should be fine here.
 *       (won't work for genmove w after w move) */
static bool
ladder_check(board_t *board, override_t *override, int rot, ladder_check_t *check)	
{
        if (!check->coord)  return true;
	
	enum stone own_color = board_to_play(board);
	enum stone ladder_color = (check->own_color ? own_color : stone_other(own_color));
	board_t b2;
	board_t *b = &b2;
	board_copy(b, board);
	
	if (!josekifix_ladder_setup(b, rot, check))
		return false;
	
	coord_t c = str2coord(check->coord);
	c = rotate_coord(c, rot);
	group_t g = board_get_2lib_neighbor(b, c, stone_other(ladder_color));
	bool good_color = (board_at(b, g) == stone_other(ladder_color));
	if (!g || !good_color)  return false;
	bool ladder = wouldbe_ladder_any(b, g, c);
	
	//board_print(b, stderr);
	bool result = (check->works ? ladder : !ladder);
	josekifix_log("joseki_override:      %s:  %s ladder at %s = %i  (%s)\n", override->name,
		      stone2str(ladder_color), coord2sstr(c), ladder, (result ? "ok" : "bad"));
	return result;
}

/* Check override ladder checks all match. */
static bool
check_override_ladder(board_t *b, override_t *override, int rot)
{
	return (ladder_check(b, override, rot, &override->ladder_check) &&
		ladder_check(b, override, rot, &override->ladder_check2));
}


/*****************************************************************************/
/* Low-level override matching */

static int
override_entry_number(override_t *overrides, override_t *o)
{
	if (!overrides)  return 1;
	
	int n = 1;
	for (int i = 0; overrides[i].name; i++) {
		if (&overrides[i] == o)
			return n;
		if (!strcmp(o->name, overrides[i].name))
			n++;
	}
	return -1; /* Not found */
}

static coord_t
str2coord_safe(char *str)
{
	if (!str || !str[0])  return pass;
	return str2coord(str);
}

/* Check override at given location (single rotation) */
static coord_t
check_override_at_rot(struct board *b, override_t *override, int rot,
		      char* coordstr, enum stone stone_color)
{
	assert(override->next[0] && override->next[0] != 'X');
	assert(coordstr[0] && coordstr[0] != 'X');

	coord_t coord = str2coord(coordstr);
	coord_t prev  = str2coord_safe(override->prev);  // optional
	coord_t next = str2coord(override->next);
	
	if (!is_pass(prev) && rotate_coord(prev, rot) != last_move(b).coord)  return pass;
	if (is_pass(next) && !external_engine_overrides_enabled)	      return pass;
		
	coord_t rcoord = rotate_coord(coord, rot);
	if (board_at(b, rcoord) == stone_color) {
		hash_t h = outer_spatial_hash_from_board(b, rcoord, last_move(b).color); /* hash with last move color */
		if (h == override->hashes[rot] &&
		    check_override_ladder(b, override, rot)) {
			want_external_engine_next = (is_pass(next) || override->external_engine);
			want_external_engine_diag_next = override->external_engine_diag;
			if (is_pass(next))
				return external_joseki_engine_genmove(b);
			return rotate_coord(next, rot);
		}
	}
	return pass;
}

/* Check override at given location (all rotations)
 * Rotation found written to @prot in case of match. */
static coord_t
check_override_at(struct board *b, override_t *override, int *prot,
		  char* coordstr, enum stone stone_color)
{
	for (int rot = 0; rot < 8; rot++) {
		coord_t c = check_override_at_rot(b, override, rot, coordstr, stone_color);
		if (!is_pass(c)) {
			if (prot)  *prot = rot;
			return c;
		}
	}
	
	return pass;
}

/* Check override around last move (single rotation) */
static coord_t
check_override_last_rot(struct board *b, override_t *override, int rot, hash_t lasth)
{
	assert(override->prev[0] && override->prev[0] != 'X');
	assert(override->next[0] && override->next[0] != 'X');

	coord_t prev = str2coord(override->prev);
	coord_t next = str2coord(override->next);
	
	if (                  rotate_coord(prev, rot) != last_move(b).coord)  return pass;
	if (is_pass(next) && !external_engine_overrides_enabled)	      return pass;
	
	if (lasth == override->hashes[rot] &&
	    check_override_ladder(b, override, rot)) {
		want_external_engine_next = (is_pass(next) || override->external_engine);
		want_external_engine_diag_next = override->external_engine_diag;
		if (is_pass(next))
			return external_joseki_engine_genmove(b);
		return rotate_coord(next, rot);
	}
	return pass;
}

/* Check override around last move (all rotations)
 * Rotation found written to @prot in case of match */
coord_t
check_override_last(struct board *b, override_t *override, int *prot, hash_t lasth)
{
	for (int rot = 0; rot < 8; rot++) {
	    coord_t c = check_override_last_rot(b, override, rot, lasth);
	    if (!is_pass(c)) {
		    if (prot)  *prot = rot;
		    return c;
	    }
	}
	
	return pass;
}

/* Check and warn if returned move is not sane... */
bool
josekifix_sane_override(struct board *b, coord_t c, char *name, int n)
{
	enum stone color = stone_other(last_move(b).color);
	if (is_pass(c))  return true;
	if (!board_is_valid_play_no_suicide(b, color, c)) {
		josekifix_log("joseki_override: %s (%s", coord2sstr(c), name);
		if (n > 1)  josekifix_log(", %i", n);
		josekifix_log(")  WARNING invalid move !!\n");
		return false;
	}
	return true;
}

static coord_t
check_override_rot_(struct board *b, override_t *override, int rot, hash_t lasth)
{
	enum stone color = last_move(b).color;
	if (override->coord_other)  return check_override_at_rot(b, override, rot, override->coord_other, color);
	if (override->coord_own)    return check_override_at_rot(b, override, rot, override->coord_own, stone_other(color));
	if (override->coord_empty)  return check_override_at_rot(b, override, rot, override->coord_empty, S_NONE);
	return check_override_last_rot(b, override, rot, lasth);
}

static coord_t
check_override_(struct board *b, override_t *override, int *prot, hash_t lasth)		
{
	enum stone color = last_move(b).color;
	if (override->coord_other)  return check_override_at(b, override, prot, override->coord_other, color);
	if (override->coord_own)    return check_override_at(b, override, prot, override->coord_own, stone_other(color));
	if (override->coord_empty)  return check_override_at(b, override, prot, override->coord_empty, S_NONE);
	return check_override_last(b, override, prot, lasth);
}

/* Check single override, making sure returned move is sane. */
coord_t
check_override(struct board *b, override_t *override, int *prot, hash_t lasth)
{
	coord_t c = check_override_(b, override, prot, lasth);

	/* Check move is sane... */
	int n = override_entry_number(joseki_overrides, override);
	if (!josekifix_sane_override(b, c, override->name, n))
		return pass;
	
	return c;
}

/* Check overrides, return first match's next move (pass if none).
 * Matching needs not be optimized at all (few entries, running once
 * at the end of genmove). So we just run through the whole list, see
 * if there's any match (we have hashes for all rotations). */
static coord_t
check_overrides_full(struct board *b, override_t overrides[], int *prot, hash_t lasth, char *title)		     
{
	if (!overrides)  return pass;
	
	for (int i = 0; overrides[i].name; i++) {
		coord_t c = check_override(b, &overrides[i], prot, lasth);
		if (!is_pass(c)) {
			if (title) {  /* log */
				int n = override_entry_number(overrides, &overrides[i]);
				josekifix_log("%s: %s (%s", title, coord2sstr(c), overrides[i].name);
				if (n != 1)  josekifix_log(", %i", n);
				josekifix_log(")\n");
			}
			return c;
		}
	}
	return pass;
}

/* Check overrides, return first match's next move */
coord_t
check_overrides(struct board *b, override_t overrides[], hash_t lasth)
{
	return check_overrides_full(b, overrides, NULL, lasth, "joseki_override");
}

/* Check overrides, return first match's next move */
coord_t
check_joseki_overrides(struct board *b, hash_t lasth)
{
	coord_t c = pass;
	
	c = check_overrides_full(b, joseki_overrides, NULL, lasth, "joseki_override");
	if (!is_pass(c))  return c;

	return pass;
}

/* Check and log logged variations */
static void
check_logged_variations(struct board *b, hash_t lasth)
{
	check_overrides_full(b, logged_variations, NULL, lasth, "joseki_variation");
}


/* Check a group of overrides matches.
 * All overrides must match (in the same rotation) for this to match.
 * Returns last entry's next move. */
coord_t
check_overrides_and(struct board *b, override_t *overrides, int *prot, hash_t lasth, bool log)		    
{
	/* Check which rotations match for first pattern, use same for the others. */
	for (int rot = 0; rot < 8; rot++) {
		coord_t c = check_override_rot_(b, &overrides[0], rot, lasth);
		if (is_pass(c))  continue;

		for (int i = 1; overrides[i].name && !is_pass(c); i++)
			c = check_override_rot_(b, &overrides[i], rot, lasth);
		if (is_pass(c))  continue;
		
		/* Check move is sane... */
		if (!josekifix_sane_override(b, c, overrides[0].name, -1))
			return pass;
		
		if (log && overrides[0].name)
			josekifix_log("joseki_override: %s (%s)\n", coord2sstr(c), overrides[0].name);
		if (prot)  *prot = rot;
		return c;
	}

	return pass;
}


/**********************************************************************************************************/

void
josekifix_init(board_t *b)
{
	if (josekifix_enabled) {
		assert(!joseki_overrides);
		external_joseki_engine_init(b);
		
		if (!external_joseki_engine) {
			/* While we could support a degraded mode where only self-contained overrides are supported,
			 * joseki fixes database is designed with external engine in mind and will not play its role
			 * without it. Disable joseki fixes and let user know. */
			if (josekifix_required)  die("josekifix required but external joseki engine missing, aborting.\n");
			fprintf(stderr, "Joseki fixes disabled: external joseki engine missing\n");
			josekifix_enabled = false;
		}
	} else if (DEBUGL(2))  fprintf(stderr, "Joseki fixes disabled\n");
}


/**********************************************************************************************************/
/* Core override checks */

coord_t
joseki_override_(struct board *b, strbuf_t *log,
		 struct ownermap *prev_ownermap, struct ownermap *ownermap,
		 bool external_engine_enabled)
{
	/* Shouldn't reach here if module disabled */
	assert(josekifix_enabled);
	
	want_external_engine_next = false;
	want_external_engine_diag_next = false;
	external_engine_overrides_enabled = external_engine_enabled;
	log_buf = log;
    
	assert(MAX_PATTERN_DIST == JOSEKIFIX_OVERRIDE_DIST);
	if (board_rsize(b) != 19)            return pass;
	
	coord_t last = last_move(b).coord;
	hash_t lasth = outer_spatial_hash_from_board(b, last, last_move(b).color);
	coord_t c = pass;

	
	/**********************************************************************************/
	/* Joseki overrides */

	/* Joseki overrides, if using dcnn */
	if (using_dcnn(b)) {
		check_logged_variations(b, lasth);
		c = check_joseki_overrides(b, lasth);
		if (!is_pass(c))  return c;
	}
	
	/**********************************************************************************/
	/* Choose inital fuseki */

	c = josekifix_initial_fuseki(b, log, lasth);
	if (!is_pass(c))  return c;
	
	return pass;
}


/**********************************************************************************************************/
/* Top-level calls */

/* Return joseki override move for current position (pass = no override).
 * only considers overrides involving a call to external joseki engine */
coord_t
joseki_override_external_engine_only(board_t *b)
{
	if (!josekifix_enabled)		return pass;
	if (!external_joseki_engine)	return pass;
	
	external_joseki_engine_genmoved = 0;
	strbuf(log, 4096);
	coord_t c = joseki_override_(b, log, NULL, NULL, true);
	
	if (!is_pass(c) && external_joseki_engine_genmoved) {
		/* display log, we have a match */
		if (DEBUGL(2))  fprintf(stderr, "%s", log->str);
		
		if (want_external_engine_next) {		/* <external joseki engine mode> on in this quadrant for next moves */
			set_external_engine_mode_on(b, last_quadrant(b));
			check_set_external_engine_mode_adjacent(b);
		}
		if (want_external_engine_diag_next)
			set_external_engine_mode_on(b, diag_quadrant(last_quadrant(b)));
					
		return c;
	}
	return pass;
}

/* Return joseki override move for current position (pass = no override).
 * ignores overrides involving external engine (ie only considers overrides
 * which specify next move explicitly. */
coord_t
joseki_override_no_external_engine(struct board *b, struct ownermap *prev_ownermap, struct ownermap *ownermap)
{
	if (!josekifix_enabled)  return pass;
	
	strbuf(log, 4096);    
	coord_t c = joseki_override_(b, log, prev_ownermap, ownermap, false);
	if (DEBUGL(2))  fprintf(stderr, "%s", log->str);	/* display log */

	if (want_external_engine_next) {		/* <external joseki engine mode> on in this quadrant for 15 moves */
	    set_external_engine_mode_on(b, last_quadrant(b));
	    check_set_external_engine_mode_adjacent(b);
	}
	if (want_external_engine_diag_next)
	    set_external_engine_mode_on(b, diag_quadrant(last_quadrant(b)));
	
	return c;
}

/* Return joseki override move for current position (pass = no override). */
static coord_t
joseki_override(struct board *b)
{
	if (!josekifix_enabled)  return pass;
	
	external_joseki_engine_genmoved = 0;
	strbuf(log, 4096);
	coord_t c = joseki_override_(b, log, NULL, NULL, true);
	if (DEBUGL(2))  fprintf(stderr, "%s", log->str);	/* display log */

	if (want_external_engine_next) {		/* <external joseki engine mode> on in this quadrant for 15 moves */
	    set_external_engine_mode_on(b, last_quadrant(b));
	    check_set_external_engine_mode_adjacent(b);
	}
	if (want_external_engine_diag_next)
	    set_external_engine_mode_on(b, diag_quadrant(last_quadrant(b)));
	
	return c;
}


/**********************************************************************************************************/

/* GTP early genmove logic: Called from GTP layer before engine genmove.
 * If there's an override involving external joseki engine we want to avoid spending
 * time in both engines.
 * 
 * So check if there's an override involving external engine.
 * - If so get final move from it. Caller should skip engine genmove entirely.
 * - Otherwise return pass. Override will be handled normally at the end of genmove (if any).
 * 
 * Also take care to apply overrides to external engine moves if in external_joseki_engine_mode,
 * they should take precedence. If there's an override still ask it for a move even though we
 * don't need it to keep game timing the same. */
coord_t
joseki_override_before_genmove(board_t *b, enum stone color)
{
	if (!josekifix_enabled)  return pass;
	
	coord_t c = pass;
	coord_t prev = last_move(b).coord;
	int quad = (is_pass(prev) ? -1 : board_quadrant(b, prev));	
	bool external_joseki_engine_mode_on = (quad != -1 && b->external_joseki_engine_moves_left_by_quadrant[quad]);
	
	if (external_joseki_engine_mode_on) {
		b->external_joseki_engine_moves_left_by_quadrant[quad]--;
		
		if (DEBUGL(3))  fprintf(stderr, "external joseki engine mode: quadrant %i, moves left: %i\n",
					quad, b->external_joseki_engine_moves_left_by_quadrant[quad]);

		/* First check overrides. */
		c = joseki_override(b);

		/* If genmoved, we have final move and we spent some time thinking, all good.  */
		if (external_joseki_engine_genmoved)  return c;

		/* Get move now then ... */
		coord_t c2 = external_joseki_engine_genmove(b);

		/* But let override take over if different. */
		if (!is_pass(c) && !is_pass(c2) && c2 != c) {   /* Keep engines in sync ! */
			c2 = c;
			external_joseki_engine_undo(b);	        /* Undo external engine move, */
			external_joseki_engine_genmoved = 0;	/* GTP layer will send play commend */
		}
		
		return c2;
	}
	
	if (is_pass(c))
		c = joseki_override_external_engine_only(b);

	return c;
}

