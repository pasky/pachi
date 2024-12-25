#include <assert.h>
#include <stdarg.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "pattern/spatial.h"
#include "tactics/util.h"
#include "tactics/2lib.h"
#include "tactics/ladder.h"
#include "josekifix/override.h"
#include "josekifix/joseki_override.h"
#include "josekifix/josekifixload.h"
#include "josekifix/josekifix_engine.h"
#include "engines/external.h"


/*****************************************************************************/
/* Init */

static bool josekifix_enabled = true;
static bool josekifix_required = false;
void disable_josekifix()      {  josekifix_enabled = false;  }
void require_josekifix()      {  josekifix_required = true;  }
bool get_josekifix_enabled()  {  return josekifix_enabled;   }
bool get_josekifix_required() {  return josekifix_required;  }

bool
josekifix_init(board_t *b)
{
	assert(!joseki_overrides);
	
	/* Load database of joseki fixes */
	if (!josekifix_load()) {
		josekifix_enabled = false;
		return false;
	}

	return true;
}


/**********************************************************************************************************/
/* External engine */


/* For each quadrant, whether to enable external engine mode (value specifies number of moves)  */
static int  wanted_external_engine_mode[4] = { 0, };

static bool external_engine_overrides_enabled = true;

#define EXTERNAL_ENGINE_MOVE -3

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
set_wanted_external_engine_mode(board_t *b, joseki_override_t *override, coord_t next, int rot)
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


/**********************************************************************************************************/
/* Logging */

static strbuf_t *log_buf = NULL;

/* All code matching overrides should use this for logging.
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


/**********************************************************************************************************/
/* Ladder checks */

/* Add ladder check setup stones */
bool
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
ladder_check(board_t *board, joseki_override_t *override, int rot, ladder_check_t *check)	
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
check_override_ladder(board_t *b, joseki_override_t *override, int rot)
{
	return (ladder_check(b, override, rot, &override->ladder_check) &&
		ladder_check(b, override, rot, &override->ladder_check2));
}


/**********************************************************************************************************/
/* Low-level override matching */

static int
override_entry_number(joseki_override_t *overrides, joseki_override_t *o)
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
override2_entry_number(joseki_override2_t *overrides, joseki_override2_t *o)
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
check_joseki_override_at_rot(struct board *b, joseki_override_t *override, int rot, char* coordstr)
{
	assert(override->next[0] && override->next[0] != 'X');
	assert(coordstr[0] && coordstr[0] != 'X');

	coord_t coord = str2coord(coordstr);
	coord_t prev  = str2coord_safe(override->prev);  // optional
	coord_t next = str2coord(override->next);
	
	if (!is_pass(prev) && rotate_coord(prev, rot) != last_move(b).coord)  return pass;
	if (is_pass(next) && !external_engine_overrides_enabled)	      return pass;
		
	coord_t rcoord = rotate_coord(coord, rot);
	hash_t h = josekifix_spatial_hash(b, rcoord, last_move(b).color); /* hash with last move color */
	if (h == override->hashes[rot] &&
	    check_override_ladder(b, override, rot)) {
		set_wanted_external_engine_mode(b, override, next, rot);
		if (is_pass(next))
			return EXTERNAL_ENGINE_MOVE;
		return rotate_coord(next, rot);
	}
	return pass;
}

/* Check override at given location (all rotations) */
static coord_t
check_joseki_override_at(struct board *b, joseki_override_t *override, char* coordstr)
{
	for (int rot = 0; rot < 8; rot++) {
		coord_t c = check_joseki_override_at_rot(b, override, rot, coordstr);
		if (!is_pass(c))
			return c;
	}
	
	return pass;
}

/* Check override around last move (single rotation) */
static coord_t
check_joseki_override_last_rot(struct board *b, joseki_override_t *override, int rot, hash_t lasth)
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

/* Check override around last move (all rotations) */
static coord_t
check_joseki_override_last(struct board *b, joseki_override_t *override, hash_t lasth)
{
	for (int rot = 0; rot < 8; rot++) {
	    coord_t c = check_joseki_override_last_rot(b, override, rot, lasth);
	    if (!is_pass(c))
		    return c;
	}
	
	return pass;
}

/* Check and warn if returned move is not sane... */
static bool
sane_joseki_override_move(struct board *b, coord_t c, char *name, int n)
{
	assert(c != EXTERNAL_ENGINE_MOVE);
	enum stone color = stone_other(last_move(b).color);
	if (is_pass(c))  return true;
	if (!board_is_valid_play_no_suicide(b, color, c)) {
		/* Override or external engine returned an invalid move.
		 * This should never happen, something very wrong is going on.
		 * Log now (not through josekifix_log() which will get silenced). */
		fprintf(stderr, "joseki_override: %s (%s", coord2sstr(c), name);
		if (n > 1)  fprintf(stderr, ", %i", n);
		fprintf(stderr, (")  WARNING invalid move !!\n");
		return false;
	}
	return true;
}

static coord_t
check_joseki_override_rot(struct board *b, joseki_override_t *override, int rot, hash_t lasth)
{
	if (override->coord)
		return check_joseki_override_at_rot(b, override, rot, override->coord);
	return check_joseki_override_last_rot(b, override, rot, lasth);
}

static coord_t
check_joseki_override_(struct board *b, joseki_override_t *override, hash_t lasth)
{
	if (override->coord)
		return check_joseki_override_at(b, override, override->coord);
	return check_joseki_override_last(b, override, lasth);
}


/**********************************************************************************************************/
/* Single override check */

/* Check single override, making sure returned move is sane. */
static coord_t
check_joseki_override(struct board *b, joseki_override_t *override, hash_t lasth)
{
	coord_t c = check_joseki_override_(b, override, lasth);

	/* Get external engine move now if needed */
	if (c == EXTERNAL_ENGINE_MOVE)
		c = external_joseki_engine_genmove(b);
	
	/* Check move is sane... */
	int n = override_entry_number(joseki_overrides, override);
	if (!sane_joseki_override_move(b, c, override->name, n))
		return pass;
	
	return c;
}

/* Check a group of overrides matches.
 * All overrides must match (in the same rotation) for this to match.
 * Returns last entry's next move. */
static coord_t
check_joseki_overrides_and(struct board *b, joseki_override_t *overrides, hash_t lasth)
{
	for (int rot = 0; rot < 8; rot++) {
		clear_wanted_external_engine_mode();	/* Cleanup in case of partial match */

		/* Check if first override matches ... */
		coord_t c = check_joseki_override_rot(b, &overrides[0], rot, lasth);
		if (is_pass(c))  continue;

		/* And all other overrides match in same rotation.  */
		for (int i = 1; overrides[i].name && !is_pass(c); i++)
			c = check_joseki_override_rot(b, &overrides[i], rot, lasth);
		if (is_pass(c))  continue;

		/* Passes all checks, get external engine move now if needed */
		if (c == EXTERNAL_ENGINE_MOVE)
			c = external_joseki_engine_genmove(b);
		
		/* Check move is sane... */
		if (!sane_joseki_override_move(b, c, overrides[0].name, -1))
			break;
		
		return c;
	}

	clear_wanted_external_engine_mode();	/* Cleanup in case of partial match */
	return pass;
}


/**********************************************************************************************************/
/* Batch override checking */

/* Check overrides, return first match's next move (pass if none).
 * Matching needs not be optimized at all (few entries, running once
 * at the end of genmove). So we just run through the whole list, see
 * if there's any match (we have hashes for all rotations). */
static coord_t
check_joseki_overrides_list(struct board *b, joseki_override_t overrides[], hash_t lasth, char *title)
{
	if (!overrides)  return pass;
	
	for (int i = 0; overrides[i].name; i++) {
		joseki_override_t *override = &overrides[i];
		coord_t c = check_joseki_override(b, override, lasth);
		if (!is_pass(c)) {
			if (title) {  /* log */
				int n = override_entry_number(overrides, override);
				josekifix_log("%s: %s (%s", title, coord2sstr(c), override->name);
				if (n != 1)  josekifix_log(", %i", n);
				josekifix_log(")\n");
			}
			return c;
		}
	}
	return pass;
}

/* Same for overrides <and> checks (joseki_override2_t) */
static coord_t
check_joseki_overrides2_list(struct board *b, joseki_override2_t overrides[], hash_t lasth, char *title)
{
	if (!overrides)  return pass;
	
	for (int i = 0; overrides[i].override1.name; i++) {
		joseki_override2_t *override = &overrides[i];
		joseki_override_t  *override1 = &override->override1;
		coord_t c = check_joseki_overrides_and(b, override1, lasth);
		if (!is_pass(c)) {
			if (title) {  /* log */
				int n = override2_entry_number(overrides, override);
				josekifix_log("%s: %s (%s", title, coord2sstr(c), override1->name);
				if (n != 1)  josekifix_log(", %i", n);
				josekifix_log(")\n");
			}
			return c;
		}
	}
	return pass;
}


/**********************************************************************************************************/
/* Top-level calls (internal) */

/* Check overrides, return first match's next move */
static coord_t
check_joseki_overrides(struct board *b, hash_t lasth)
{
	coord_t c = pass;
	
	/* <and> checks first */
	c = check_joseki_overrides2_list(b, joseki_overrides2, lasth, "joseki_override");
	if (!is_pass(c))  return c;

	/* regular overrides */
	c = check_joseki_overrides_list(b, joseki_overrides, lasth, "joseki_override");
	if (!is_pass(c))  return c;

	return pass;
}

/* Check and log logged variations */
static void
check_logged_variations(struct board *b, hash_t lasth)
{
	/* <and> checks first */	
	check_joseki_overrides2_list(b, logged_variations2, lasth, "joseki_variation");
	check_joseki_overrides_list(b, logged_variations, lasth, "joseki_variation");
}

static coord_t
joseki_override_(struct board *b, strbuf_t *log,
		 struct ownermap *prev_ownermap, struct ownermap *ownermap,
		 bool external_engine_enabled)
{
	/* Shouldn't reach here if module disabled */
	assert(josekifix_enabled);

	clear_wanted_external_engine_mode();
	external_engine_overrides_enabled = external_engine_enabled;
	log_buf = log;
    
	if (board_rsize(b) != 19)            return pass;
	
	coord_t last = last_move(b).coord;
	hash_t lasth = josekifix_spatial_hash(b, last, last_move(b).color);
	coord_t c = pass;

	
	/**********************************************************************************/
	/* Joseki overrides */

	/* Joseki overrides */
	check_logged_variations(b, lasth);
	c = check_joseki_overrides(b, lasth);
	if (!is_pass(c))  return c;
	
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
	assert(josekifix_enabled);
	external_joseki_engine_genmoved = false;
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
	assert(josekifix_enabled);
	strbuf(log, 4096);    
	coord_t c = joseki_override_(b, log, prev_ownermap, ownermap, false);
	if (DEBUGL(2))  fprintf(stderr, "%s", log->str);	/* display log */

	commit_wanted_external_engine_mode(b);
	return c;
}

/* Return joseki override move for current position (pass = no override). */
coord_t
joseki_override(struct board *b)
{
	assert(josekifix_enabled);
	external_joseki_engine_genmoved = false;
	strbuf(log, 4096);
	coord_t c = joseki_override_(b, log, NULL, NULL, true);
	if (DEBUGL(2))  fprintf(stderr, "%s", log->str);	/* display log */

	commit_wanted_external_engine_mode(b);
	return c;
}


