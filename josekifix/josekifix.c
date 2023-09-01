#include <assert.h>
#include <stdarg.h>
#include <math.h>

#define DEBUG
#include "board.h"
#include "random.h"
#include "pattern/spatial.h"
#include "ownermap.h"
#include "engine.h"
#include "dcnn/dcnn.h"
#include "joseki/joseki.h"
#include "josekifix/josekifix.h"
#include "tactics/util.h"
#include "tactics/2lib.h"
#include "tactics/ladder.h"
#include "pachi.h"
#include "version.h"
#include "engines/external.h"

coord_t joseki_override_(struct board *b, strbuf_t *log,
			 struct ownermap *prev_ownermap, struct ownermap *ownermap,
			 bool external_engine_enabled);


static bool josekifix_enabled = true;
static bool josekifix_required = false;
void disable_josekifix()  {  josekifix_enabled = false;  }
void require_josekifix()  {  josekifix_required = true;  }


/*****************************************************************************/
/* Override list stuff */

/* Representation of an <and> check (2 overrides).
 * Terminating null kept for convenience */
typedef struct {
	override_t override1;
	override_t override2;
	override_t null;
} override2_t;

/* Growable list of overrides.
 * Ensures trailing null override always available (if zero'ed initially) */
typedef struct {
	override_t *overrides;
	int          alloc;
	int          len;
} override_list_t;

/* Growable list of overrides (2 overrides <and> check)
 * Ensures trailing null override always available (if zero'ed initially) */
typedef struct {
	override2_t *overrides;
	int          alloc;
	int          len;
} override2_list_t;

/* Overrides and logged variations loaded at startup. */
override_list_t  joseki_overrides   = { 0, };
override2_list_t joseki_overrides2  = { 0, };	/* <and> checks */
override_list_t  logged_variations  = { 0, };
override2_list_t logged_variations2 = { 0, };	/* <and> checks */


/* Append override to list, realloc if necessary.  (override data is copied) */
static void
override_list_add(override_list_t *list, override_t *override)
{
	if (list->len + 2 > list->alloc) {  /* realloc */
		size_t size = sizeof(*override);
		int prev_n = list->alloc;
		list->alloc *= 2;
		list->alloc = MAX(list->alloc, 16);
		int n = list->alloc;
		list->overrides = crealloc(list->overrides, n * size);
		memset(list->overrides + prev_n, 0, (n - prev_n) * size);   /* zero newly allocated memory ! */
	}
	
	list->overrides[list->len++] = *override;
}

/* Append override to list, realloc if necessary.  (override data is copied) */
static void
override2_list_add(override2_list_t *list, override2_t *override)
{
	if (list->len + 2 > list->alloc) {  /* realloc */
		size_t size = sizeof(*override);
		int prev_n = list->alloc;
		list->alloc *= 2;
		list->alloc = MAX(list->alloc, 16);
		int n = list->alloc;
		list->overrides = crealloc(list->overrides, n * size);
		memset(list->overrides + prev_n, 0, (n - prev_n) * size);   /* zero newly allocated memory ! */
	}
	
	list->overrides[list->len++] = *override;
}


/*****************************************************************************/
/* External engine */

char     *external_joseki_engine_cmd = "katago gtp";
engine_t *external_joseki_engine = NULL;
int       external_joseki_engine_genmoved = 0;

/* For each quadrant, whether to enable external engine mode (value specifies number of moves)  */
static int  wanted_external_engine_mode[4] = { 0, };

static bool external_engine_overrides_enabled = true;

#define EXTERNAL_ENGINE_MOVE -3

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

/* <external joseki engine mode> on in this quadrant for next moves */
static void
set_external_engine_mode_quad(board_t *b, int quadrant, int moves)
{
	assert(quadrant >= 0 && quadrant <= 3);
	b->external_joseki_engine_moves_left_by_quadrant[quadrant] = moves;
}

/* If last move near middle, turn on adjacent quadrant as well */
static void
check_set_external_engine_mode_adjacent_quad(board_t *b, int moves)
{	
	int x = coord_x(last_move(b).coord);
	int y = coord_y(last_move(b).coord);
	int mid = (board_rsize(b) + 1) / 2;
	int adx = abs(mid - x);
	int ady = abs(mid - y);
	
	if (adx < ady && adx <= 2) {
		if (y > mid) {
			set_external_engine_mode_quad(b, 0, moves);
			set_external_engine_mode_quad(b, 1, moves);
		}
		if (y < mid) {
			set_external_engine_mode_quad(b, 2, moves);
			set_external_engine_mode_quad(b, 3, moves);
		}
	}
	
	if (ady < adx && ady <= 2) {
		if (x < mid) {
			set_external_engine_mode_quad(b, 0, moves);
			set_external_engine_mode_quad(b, 3, moves);
		}
		if (x > mid) {
			set_external_engine_mode_quad(b, 1, moves);
			set_external_engine_mode_quad(b, 2, moves);
		}
	}
}

#if 0
/* <external joseki engine mode> on in all quadrannts */
static void
set_external_engine_mode_all_quadrants(board_t *b, int moves)
{
	for (int quad = 0; quad < 4; quad++)
		set_external_engine_mode_quad(b, quad, moves);
}
#endif

static void
clear_wanted_external_engine_mode(void)
{
	for (int q = 0; q < 4; q++)
		wanted_external_engine_mode[q] = 0;
}

static void
set_wanted_external_engine_mode(board_t *b, override_t *override, coord_t next, int rot)
{
	bool have = false;
	for (int q = 0; q < 4; q++)
		if (override->external_engine_mode[q]) {
			have = true;
			wanted_external_engine_mode[rotate_quadrant(q, rot)] = override->external_engine_mode[q];
		}
	if (have)  return;	/* explicit setting takes precedence if set */

	if (is_pass(next))	/* pass as next move = enable external engine mode in last quadrant */
		wanted_external_engine_mode[last_quadrant(b)] = DEFAULT_EXTERNAL_ENGINE_MOVES;
}

static void
commit_wanted_external_engine_mode(board_t *b)
{
	for (int q = 0; q < 4; q++) {
		int moves = wanted_external_engine_mode[q];
		
		if (moves) {	/* enable external joseki engine mode in this quadrant */
			set_external_engine_mode_quad(b, q, moves);
			if (q == last_quadrant(b))
				check_set_external_engine_mode_adjacent_quad(b, moves);
		}
	}
}


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

/* Fill in override hashes from board position (all rotations) */
void
joseki_override_fill_hashes(override_t *override, board_t *b)
{
	enum stone color = last_move(b).color;	// last move color
	
	coord_t around = last_move(b).coord;
	if (override->coord_empty)  around = str2coord(override->coord_empty);
	if (override->coord_own)    around = str2coord(override->coord_own);
	if (override->coord_other)  around = str2coord(override->coord_other);
	
	for (int rot = 0; rot < 8; rot++)
		override->hashes[rot] = outer_spatial_hash_from_board_rot_d(b, around, color, rot, MAX_PATTERN_DIST);	
}


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
		if (r < 0)  return false;		// shouldn't happen really
	}
	
	for (int i = 0; i < n; i++) {
		char *coordstr = check->setup_other[i];
		if (!coordstr)  break;
		coord_t c = str2coord(coordstr);
		c = rotate_coord(c, rot);
		move_t m = { c, stone_other(color) };
		//josekifix_log("setup %s %s\n", stone2str(stone_other(color)), coord2str(c));
		int r = board_play(b, &m);
		if (r < 0)  return false;		// shouldn't happen really
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

static int
override2_entry_number(override2_t *overrides, override2_t *o)
{
	if (!overrides)  return 1;
	
	int n = 1;
	for (int i = 0; overrides[i].override1.name; i++) {
		if (&overrides[i] == o)
			return n;
		if (!strcmp(o->override1.name, overrides[i].override1.name))
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
			set_wanted_external_engine_mode(b, override, next, rot);
			if (is_pass(next))
				return EXTERNAL_ENGINE_MOVE;
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
		set_wanted_external_engine_mode(b, override, next, rot);
		if (is_pass(next))
			return EXTERNAL_ENGINE_MOVE;
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
	assert(c != EXTERNAL_ENGINE_MOVE);
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

coord_t
check_override_rot(struct board *b, override_t *override, int rot, hash_t lasth)
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

	/* Get external engine move now if needed */
	if (c == EXTERNAL_ENGINE_MOVE)
		c = external_joseki_engine_genmove(b);
	
	/* Check move is sane... */
	int n = override_entry_number(joseki_overrides.overrides, override);
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

/* Same for overrides <and> checks (override2_t) */
static coord_t
check_overrides2_full(struct board *b, override2_t overrides[], int *prot, hash_t lasth, char *title)
{
	if (!overrides)  return pass;
	
	for (int i = 0; overrides[i].override1.name; i++) {
		coord_t c = check_overrides_and(b, (override_t*)&overrides[i], prot, lasth, false);
		if (!is_pass(c)) {
			if (title) {  /* log */
				int n = override2_entry_number(overrides, &overrides[i]);
				josekifix_log("%s: %s (%s", title, coord2sstr(c), overrides[i].override1.name);
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
	
	/* <and> checks first */
	c = check_overrides2_full(b, joseki_overrides2.overrides, NULL, lasth, "joseki_override");
	if (!is_pass(c))  return c;

	/* regular overrides */
	c = check_overrides_full(b, joseki_overrides.overrides, NULL, lasth, "joseki_override");
	if (!is_pass(c))  return c;

	return pass;
}

/* Check and log logged variations */
static void
check_logged_variations(struct board *b, hash_t lasth)
{
	/* <and> checks first */	
	check_overrides2_full(b, logged_variations2.overrides, NULL, lasth, "joseki_variation");
	check_overrides_full(b, logged_variations.overrides, NULL, lasth, "joseki_variation");
}


/* Check a group of overrides matches.
 * All overrides must match (in the same rotation) for this to match.
 * Returns last entry's next move. */
coord_t
check_overrides_and(struct board *b, override_t *overrides, int *prot, hash_t lasth, bool log)
{
	for (int rot = 0; rot < 8; rot++) {
		clear_wanted_external_engine_mode();	/* Cleanup in case of partial match */

		/* Check if first override matches ... */
		coord_t c = check_override_rot(b, &overrides[0], rot, lasth);
		if (is_pass(c))  continue;

		/* And all other overrides match in same rotation.  */
		for (int i = 1; overrides[i].name && !is_pass(c); i++)
			c = check_override_rot(b, &overrides[i], rot, lasth);
		if (is_pass(c))  continue;

		/* Passes all checks, get external engine move now if needed */
		if (c == EXTERNAL_ENGINE_MOVE)
			c = external_joseki_engine_genmove(b);
		
		/* Check move is sane... */
		if (!josekifix_sane_override(b, c, overrides[0].name, -1))
			break;
		
		if (log && overrides[0].name)
			josekifix_log("joseki_override: %s (%s)\n", coord2sstr(c), overrides[0].name);
		if (prot)  *prot = rot;
		return c;
	}

	clear_wanted_external_engine_mode();	/* Cleanup in case of partial match */
	return pass;
}


/**********************************************************************************************************/
/* Override printing, comparing */

static void
print_ladder_check(char *idx, ladder_check_t *c)
{
	char *color = (c->own_color ? "own" : "other");
	char *works = (c->works ? "" : "no");	
	
	fprintf(stderr, "  %sladder_%s%s = %s  [ ", works, color, idx, c->coord);
	for (int i = 0; c->setup_own[i]; i++)
		fprintf(stderr, "%s ", c->setup_own[i]);
	fprintf(stderr, "]  [ ");
	for (int i = 0; c->setup_other[i]; i++)
		fprintf(stderr, "%s ", c->setup_other[i]);
	fprintf(stderr, "]\n");
}

void
joseki_override_print(override_t *override, char *section)
{
	fprintf(stderr, "%s:\n", section);
	fprintf(stderr, "  name = \"%s\"\n", override->name);
	fprintf(stderr, "  prev = %s\n", override->prev);
	fprintf(stderr, "  next = %s\n", override->next);
	
	if (override->coord_own)	fprintf(stderr, "  coord_own = %s\n", override->coord_own);
	if (override->coord_other)	fprintf(stderr, "  coord_other = %s\n", override->coord_other);
	if (override->coord_empty)	fprintf(stderr, "  coord_empty = %s\n", override->coord_empty);
		

	if (override->ladder_check.coord)  {  print_ladder_check("",  &override->ladder_check);  }
	if (override->ladder_check2.coord) {  print_ladder_check("2", &override->ladder_check2);  }

	fprintf(stderr, "  external_engine = [ ");
	for (int q = 0; q < 4; q++)
		fprintf(stderr, "%i ", override->external_engine_mode[q]);
	fprintf(stderr, "]\n");

	fprintf(stderr, "  hashes = { ");
	for (int i = 0; i < 8; i++) {
		fprintf(stderr, "0x%"PRIhash"%s ", override->hashes[i], (i != 7 ? "," : " }\n"));
		if (i == 3)  fprintf(stderr, "\n             ");
	}
}

/* like strcmp() but handle NULLs */
static int
same_str(char *s1, char *s2)
{
	return ((!s1 && !s2) ||
		(s1 && s2 && !strcmp(s1, s2)));
}

static int
ladder_check_cmp(ladder_check_t *c1, ladder_check_t *c2)
{
	if (!same_str(c1->coord, c2->coord) ||
	    c1->works != c2->works ||
	    c1->own_color != c2->own_color)
		return 1;
	
	for (int i = 0; i < JOSEKIFIX_LADDER_SETUP_MAX; i++) {
		if (!same_str(c1->setup_own[i], c2->setup_own[i]))
			return 1;
		if (!c1->setup_own[i] && !c2->setup_own[i])
			break;
	}
	
	for (int i = 0; i < JOSEKIFIX_LADDER_SETUP_MAX; i++) {
		if (!same_str(c1->setup_other[i], c2->setup_other[i]))
			return 1;
		if (!c1->setup_other[i] && !c2->setup_other[i])
			break;
	}

	return 0;
}

/* Compare 2 overrides (don't use that for sorting!) */
static int
override_cmp(override_t *o1, override_t *o2)
{
	return !(same_str(o1->prev, o2->prev) &&
		 same_str(o1->next, o2->next) &&
		 !memcmp(o1->hashes, o2->hashes, sizeof(o1->hashes)) &&
		 same_str(o1->coord_own, o2->coord_own) &&
		 same_str(o1->coord_other, o2->coord_other) &&
		 same_str(o1->coord_empty, o2->coord_empty) &&
		 !ladder_check_cmp(&o1->ladder_check, &o2->ladder_check) &&
		 !ladder_check_cmp(&o1->ladder_check2, &o2->ladder_check2) &&
		 !memcmp(o1->external_engine_mode, o2->external_engine_mode, sizeof(o1->external_engine_mode)));
}


/**********************************************************************************************************/
/* Load from file */

static void
ladder_sanity_check(board_t *board, ladder_check_t *check, override_t *override)
{
	board_t b2;  board_copy(&b2, board);
	board_t *b = &b2;

	/* Check coords are valid */
	
	if (!valid_str_coord(check->coord)) {
		board_print(board, stderr);	/* orig board, without setup stones */
		die("josekifix: \"%s\": invalid ladder coord '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, check->coord);
	}
	
	int n = JOSEKIFIX_LADDER_SETUP_MAX;
	for (int i = 0; i < n && check->setup_own[i]; i++)
		if (!valid_str_coord(check->setup_own[i])) {
			board_print(board, stderr);
			die("josekifix: \"%s\": invalid ladder setup_own coord '%s', aborting. (run with -d5 to see previous moves)\n",
			    override->name, check->setup_own[i]);
		}

	for (int i = 0; i < n && check->setup_other[i]; i++)
		if (!valid_str_coord(check->setup_other[i])) {
			board_print(board, stderr);
			die("josekifix: \"%s\": invalid ladder setup_other coord '%s', aborting. (run with -d5 to see previous moves)\n",
			    override->name, check->setup_other[i]);
		}
	
	/* Check board setup is sane */
	
	if (!josekifix_ladder_setup(b, 0, check)) {
		board_print(board, stderr);
		die("josekifix: \"%s\": bad ladder setup, some invalid move(s), aborting. (run with -d5 to see previous moves)\n",
		    override->name);
	}
	
	enum stone own_color = board_to_play(board);
	enum stone ladder_color = (check->own_color ? own_color : stone_other(own_color));
	coord_t c = str2coord(check->coord);
	group_t g = board_get_2lib_neighbor(b, c, stone_other(ladder_color));

	if (!g) {
		board_print(board, stderr);	/* orig board */
		board_print(b, stderr);		/* ladder setup board */
		die("josekifix: \"%s\": bad ladder check, no ladder at %s ! aborting. (run with -d5 to see previous moves)\n",
		    override->name, check->coord);
	}

	bool good_color = (board_at(b, g) == stone_other(ladder_color));
	if (!good_color) {
		board_print(board, stderr);	/* orig board */
		board_print(b, stderr);		/* ladder setup board */
		die("josekifix: \"%s\": ladder check at %s: wrong color, aborting. (run with -d5 to see previous moves)\n",
		    override->name, check->coord);
	}
}

/* Common sanity checks for [override] and [log] sections */
static char*
common_sanity_checks(board_t *b, override_t *override)
{
	if (!override->name || !override->name[0]) {
		board_print(b, stderr);
		die("josekifix: this override has no name, aborting. (run with -d5 to see previous moves)\n");
	}

	if (!valid_str_coord(override->prev) && strcmp(override->prev, "pass")) {
		board_print(b, stderr);
		die("josekifix: \"%s\": invalid prev move '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, override->prev);
	}

	if (!valid_str_coord(override->next) && strcmp(override->next, "pass")) {
		board_print(b, stderr);
		die("josekifix: \"%s\": invalid next move '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, override->next);
	}

	/* Already checked by josekifixscan but doesn't hurt ... */
	char *around_str = NULL;
	if (override->coord_own)	around_str = override->coord_own;
	if (override->coord_other)	around_str = override->coord_other;
	if (override->coord_empty)	around_str = override->coord_empty;
	
	if (around_str && !valid_str_coord(around_str)) {
		board_print(b, stderr);
		die("josekifix: \"%s\": invalid around coord '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, around_str);
	}
		
	if (override->ladder_check.coord)   ladder_sanity_check(b, &override->ladder_check, override);
	if (override->ladder_check2.coord)  ladder_sanity_check(b, &override->ladder_check2, override);
	
	/* Not checking hashes ... */

	return around_str;
}

/* Check override is sane, help locate bad override otherwise. */
static void
override_sanity_checks(board_t *b, override_t *override)
{
	/* Common checks first */
	char *around_str = common_sanity_checks(b, override);
	
	/* Warn if moves are too far apart. */
	coord_t prev = str2coord(override->prev);
	coord_t next = str2coord(override->next);
	coord_t around = (around_str ? str2coord(around_str) : pass);

#define point_dist(a, b)  (is_pass(a) || is_pass(b) ? 0 : roundf(coord_distance(a, b)))
	
	int dist = point_dist(prev, next);
	if (dist > 8) {
		board_print(b, stderr);
		fprintf(stderr, "josekifix: \"%s\": big distance between prev move (%s) and next move (%s), bad override coords ?\n\n",
			override->name, override->prev, override->next);
	}

	dist = point_dist(prev, around);
	if (dist > 6) {
		board_print(b, stderr);		
		fprintf(stderr, "josekifix: \"%s\": big distance between prev move (%s) and around coord (%s), bad override coords ?\n\n",
			override->name, override->prev, coord2sstr(around));
	}

	dist = point_dist(next, around);
	if (dist > 6) {
		board_print(b, stderr);		
		fprintf(stderr, "josekifix: \"%s\": big distance between next move (%s) and around coord (%s), bad override coords ?\n\n",
			override->name, override->next, coord2sstr(around));
	}

	// TODO if next move, check it's inside match pattern ...
}

/* Check log is sane, help locate bad override otherwise. */
static void
log_sanity_checks(board_t *b, override_t *override)
{
	/* Common checks first */
	char *around_str = common_sanity_checks(b, override);

	/* Warn if moves are too far apart.
	 * (only check prev and around, logs have dummy next) */
	coord_t prev = str2coord(override->prev);
	coord_t around = (around_str ? str2coord(around_str) : pass);
	
	int dist = point_dist(prev, around);
	if (dist > 6) {
		board_print(b, stderr);
		fprintf(stderr, "josekifix: \"%s\": big distance between prev move (%s) and around coord (%s), bad override coords ?\n\n",
			override->name, override->prev, coord2sstr(around));
	}
}

/* Add a new override to the set of checked overrides. */
void
josekifix_add_override(board_t *b, override_t *override)
{
	/* Don't add duplicates */
	for (int i = 0; i < joseki_overrides.len; i++)
		if (!override_cmp(override, &joseki_overrides.overrides[i]))
			return;	

	override_sanity_checks(b, override);
	override_list_add(&joseki_overrides, override);
}

/* Add new override and check (2 overrides) to the set of checked overrides. */
void
josekifix_add_override_and(board_t *b, override_t *override1, override_t *override2)
{
	/* Don't add duplicates */
	for (int i = 0; i < joseki_overrides2.len; i++)
		if (!override_cmp(override1, &joseki_overrides2.overrides[i].override1) &&
		    !override_cmp(override2, &joseki_overrides2.overrides[i].override2))
			return;	

	override_sanity_checks(b, override1);
	/* Skip override2 sanity check (long distance warning but that's ok here). */
	//override_sanity_checks(b, override2);
	
	override2_t and_check = { 0, };
	and_check.override1 = *override1;
	and_check.override2 = *override2;
	
	override2_list_add(&joseki_overrides2, &and_check);
}

/* Add a new logged variation to the set of logged variations.
 * They work like overrides except they only affect logging :
 * They don't interfere with game moves. */
void
josekifix_add_logged_variation(board_t *b, override_t *log)
{
	// in this case override must have a next move (not pass),
	// even though it's ignored.
	log->next = "A1";
	
	// don't add duplicates
	for (int i = 0; i < logged_variations.len; i++)
		if (!override_cmp(log, &logged_variations.overrides[i]))
			return;	

	log_sanity_checks(b, log);
	override_list_add(&logged_variations, log);
}

void
josekifix_add_logged_variation_and(board_t *b, override_t *log1, override_t *log2)
{
	// in this case overrides must have a next move (not pass),
	// even though it's ignored.
	log1->next = "A1";
	log2->next = "A1";
	
	// don't add duplicates
	for (int i = 0; i < logged_variations2.len; i++)
		if (!override_cmp(log1, &logged_variations2.overrides[i].override1) &&
		    !override_cmp(log2, &logged_variations2.overrides[i].override2))
			return;	

	log_sanity_checks(b, log1);
	/* Skip override2 sanity check (long distance warning but that's ok here). */
	//log_sanity_checks(b, log2);

	override2_t and_check = { 0, };
	and_check.override1 = *log1;
	and_check.override2 = *log2;

	override2_list_add(&logged_variations2, &and_check);
}


/* Load josekifix overrides from file.
 * Debugging: to get a dump of all entries, run                      'pachi -d4'
 *            to get a dump of all entries + earlier positions, run  'pachi -d5'  */
static void
josekifix_load(void)
{
	const char *fname = "josekifix.gtp";
	FILE *f = fopen_data_file(fname, "r");
	if (!f) {
		if (DEBUGL(3))		 perror(fname);
		if (DEBUGL(2))		 fprintf(stderr, "Joseki fixes: file %s missing\n", fname);
		if (josekifix_required)  die("josekifix required but %s missing, aborting.\n", fname);
		if (DEBUGL(2))		 fprintf(stderr, "Joseki fixes disabled\n");
		josekifix_enabled = false;
		return;
	}
	if (DEBUGL(2))  fprintf(stderr, "Loading joseki fixes ...\n");

	DEBUG_QUIET();		// turn off debugging (only want debug msg inside josekifixscan engine)
	board_t *b = board_new(19, NULL);
	engine_t e;  engine_init(&e, E_JOSEKIFIXLOAD, NULL, NULL);
	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_none;
	ti[S_WHITE] = ti_none;
	char buf[4096];
	gtp_t gtp;  gtp_init(&gtp, b);
	for (int lineno = 1; fgets(buf, 4096, f); lineno++) {
		/* Pachi version check */
		if (str_prefix("# Pachi ", buf)) {
			double wanted  = atof(buf + strlen("# Pachi "));
			if (saved_debug_level > 3) fprintf(stderr, "checking version >= %.2f\n", wanted);
			if (PACHI_VERNUM < wanted)
				die("%s: need pachi version >= %.2f\n", fname, wanted);
		}
		
		gtp.quiet = true;  // XXX fixme, refactor
		enum parse_code c = gtp_parse(&gtp, b, &e, ti, buf);  /* quiet */
		/* TODO check gtp command didn't gtp_error() also, will still return P_OK on error ... */
		if (c != P_OK && c != P_ENGINE_RESET)
			die("%s:%i  gtp command '%s' failed, aborting.\n", fname, lineno, buf);
	}
	engine_done(&e);
	board_delete(&b);
	DEBUG_QUIET_END();

	if (DEBUGL(3))  fprintf(stderr, "Loaded %i overrides (and: %i), %i logged variations (and: %i)\n", joseki_overrides.len, joseki_overrides2.len, logged_variations.len, logged_variations2.len);
	else if (DEBUGL(2))  fprintf(stderr, "Loaded %i overrides.\n", joseki_overrides.len);

	fclose(f);
}

void
josekifix_init(board_t *b)
{
	if (josekifix_enabled) {
		assert(!joseki_overrides.overrides);
		external_joseki_engine_init(b);
		
		if (!external_joseki_engine) {
			/* While we could support a degraded mode where only self-contained overrides are supported,
			 * joseki fixes database is designed with external engine in mind and will not play its role
			 * without it. Disable joseki fixes and let user know. */
			if (josekifix_required)  die("josekifix required but external joseki engine missing, aborting.\n");
			if (DEBUGL(1)) fprintf(stderr, "Joseki fixes disabled: external joseki engine missing\n");
			josekifix_enabled = false;
		} else  /* Load database of joseki fixes */
			josekifix_load();
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

	clear_wanted_external_engine_mode();
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
	
	/* Kill 3-3 invasion */
	if (prev_ownermap) {
		c = josekifix_kill_3_3_invasion(b, prev_ownermap, lasth);
		if (!is_pass(c))  return c;
	}
	
	/**********************************************************************************/
	/* Fuseki overrides */

	/* Influence-only fusekis countermeasures */
	if (playing_against_influence_fuseki(b)) {
		c = external_joseki_engine_genmove(b);
		if (!b->influence_fuseki_by_quadrant[last_quadrant(b)]++)
			josekifix_log("joseki override: %s (influence fuseki)\n", coord2sstr(c));
		wanted_external_engine_mode[last_quadrant(b)] = DEFAULT_EXTERNAL_ENGINE_MOVES;
		return c;
	}

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
		
		commit_wanted_external_engine_mode(b);
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

	commit_wanted_external_engine_mode(b);
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

	commit_wanted_external_engine_mode(b);
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
	int quad = last_quadrant(b);
	bool external_joseki_engine_mode_on = b->external_joseki_engine_moves_left_by_quadrant[quad];
	
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

