#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "version.h"
#include "engine.h"
#include "move.h"
#include "tactics/util.h"
#include "tactics/2lib.h"
#include "pattern/spatial.h"
#include "josekifix/josekifix.h"
#include "josekifix/josekifixload.h"


/**********************************************************************************************************/
/* Override list stuff */

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
static override_list_t  joseki_overrides_list   = { 0, };
static override2_list_t joseki_overrides2_list  = { 0, };	/* <and> checks */
static override_list_t  logged_variations_list  = { 0, };
static override2_list_t logged_variations2_list = { 0, };	/* <and> checks */


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

static void
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
	
	if (!valid_coord(check->coord)) {
		board_print(board, stderr);	/* orig board, without setup stones */
		die("josekifix: \"%s\": invalid ladder coord '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, check->coord);
	}
	
	int n = JOSEKIFIX_LADDER_SETUP_MAX;
	for (int i = 0; i < n && check->setup_own[i]; i++)
		if (!valid_coord(check->setup_own[i])) {
			board_print(board, stderr);
			die("josekifix: \"%s\": invalid ladder setup_own coord '%s', aborting. (run with -d5 to see previous moves)\n",
			    override->name, check->setup_own[i]);
		}

	for (int i = 0; i < n && check->setup_other[i]; i++)
		if (!valid_coord(check->setup_other[i])) {
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

	if (!valid_coord(override->prev) && strcmp(override->prev, "pass")) {
		board_print(b, stderr);
		die("josekifix: \"%s\": invalid prev move '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, override->prev);
	}

	if (!valid_coord(override->next) && strcmp(override->next, "pass")) {
		board_print(b, stderr);
		die("josekifix: \"%s\": invalid next move '%s', aborting. (run with -d5 to see previous moves)\n",
		    override->name, override->next);
	}

	/* Already checked by add_override() but doesn't hurt ... */
	char *around_str = NULL;
	if (override->coord_own)	around_str = override->coord_own;
	if (override->coord_other)	around_str = override->coord_other;
	if (override->coord_empty)	around_str = override->coord_empty;
	
	if (around_str && !valid_coord(around_str)) {
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
static void
josekifix_add_override(board_t *b, override_t *override)
{
	/* Don't add duplicates */
	for (int i = 0; i < joseki_overrides_list.len; i++)
		if (!override_cmp(override, &joseki_overrides_list.overrides[i]))
			return;	

	override_sanity_checks(b, override);
	override_list_add(&joseki_overrides_list, override);
}

/* Add new override and check (2 overrides) to the set of checked overrides. */
static void
josekifix_add_override_and(board_t *b, override_t *override1, override_t *override2)
{
	/* Don't add duplicates */
	for (int i = 0; i < joseki_overrides2_list.len; i++)
		if (!override_cmp(override1, &joseki_overrides2_list.overrides[i].override1) &&
		    !override_cmp(override2, &joseki_overrides2_list.overrides[i].override2))
			return;	

	override_sanity_checks(b, override1);
	/* Skip override2 sanity check (long distance warning but that's ok here). */
	//override_sanity_checks(b, override2);
	
	override2_t and_check = { 0, };
	and_check.override1 = *override1;
	and_check.override2 = *override2;
	
	override2_list_add(&joseki_overrides2_list, &and_check);
}

/* Add a new logged variation to the set of logged variations.
 * They work like overrides except they only affect logging :
 * They don't interfere with game moves. */
static void
josekifix_add_logged_variation(board_t *b, override_t *log)
{
	// in this case override must have a next move (not pass),
	// even though it's ignored.
	log->next = "A1";
	
	// don't add duplicates
	for (int i = 0; i < logged_variations_list.len; i++)
		if (!override_cmp(log, &logged_variations_list.overrides[i]))
			return;	

	log_sanity_checks(b, log);
	override_list_add(&logged_variations_list, log);
}

static void
josekifix_add_logged_variation_and(board_t *b, override_t *log1, override_t *log2)
{
	// in this case overrides must have a next move (not pass),
	// even though it's ignored.
	log1->next = "A1";
	log2->next = "A1";
	
	// don't add duplicates
	for (int i = 0; i < logged_variations2_list.len; i++)
		if (!override_cmp(log1, &logged_variations2_list.overrides[i].override1) &&
		    !override_cmp(log2, &logged_variations2_list.overrides[i].override2))
			return;	

	log_sanity_checks(b, log1);
	/* Skip override2 sanity check (long distance warning but that's ok here). */
	//log_sanity_checks(b, log2);

	override2_t and_check = { 0, };
	and_check.override1 = *log1;
	and_check.override2 = *log2;

	override2_list_add(&logged_variations2_list, &and_check);
}

override_t  *joseki_overrides = NULL;
override2_t *joseki_overrides2 = NULL;
override_t  *logged_variations = NULL;
override2_t *logged_variations2 = NULL;

/* Load josekifix overrides from file.
 * Debugging: to get a dump of all entries, run                      'pachi -d4'
 *            to get a dump of all entries + earlier positions, run  'pachi -d5'  */
bool
josekifix_load(void)
{
	const char *fname = "josekifix.gtp";
	FILE *f = fopen_data_file(fname, "r");
	if (!f) {
		if (DEBUGL(3))			perror(fname);
		if (DEBUGL(2))			fprintf(stderr, "Joseki fixes: file %s missing\n", fname);
		if (get_josekifix_required())	die("josekifix required but %s missing, aborting.\n", fname);
		if (DEBUGL(2))			fprintf(stderr, "Joseki fixes disabled\n");
		return false;
	}
	if (DEBUGL(2))  fprintf(stderr, "Loading joseki fixes ...\n");

	DEBUG_QUIET();		// Turn off debugging (only want debug msg inside josekifixload engine)
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

	if (DEBUGL(3))  fprintf(stderr, "Loaded %i overrides (and: %i), %i logged variations (and: %i)\n",
				joseki_overrides_list.len, joseki_overrides2_list.len,
				logged_variations_list.len, logged_variations2_list.len);
	else if (DEBUGL(2))  fprintf(stderr, "Loaded %i overrides.\n", joseki_overrides_list.len);

	joseki_overrides   = joseki_overrides_list.overrides;
	joseki_overrides2  = joseki_overrides2_list.overrides;
	logged_variations  = logged_variations_list.overrides;
	logged_variations2 = logged_variations2_list.overrides;

	fclose(f);
	return true;
}


/**********************************************************************************************************/

/* Fill in override hashes from board position (all rotations) */
static void
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


/**********************************************************************************************************/
/* Parsing */

#define board_captures(b)  (b->captures[S_BLACK] + b->captures[S_WHITE])

typedef struct {
	char *name;
	char *value;
} var_t;


/* format:
 *   override name1=value1|name2=value2|name3=value3...    or
 *   log name1=value1|name2=value2|name3=value3...         */
static var_t*
parse_josekifix_vars(board_t *b, char *str, int *vars_len)
{
	static var_t vars[16];  memset(vars, 0, sizeof(vars));
	*vars_len = 0;

	char *end = strchr(str, ' ');
	if (end) {
		*end = 0;
		str = end + 1;
	}

	int k;
	for (k = 0; 1; k++) {
		char *end = strchr(str, '|');     //  field separator
		if (!end)  end = str + strlen(str);
		
		char *equal = strchr(str, '=');
		bool name_only = (!equal || equal > end);
		if (name_only) {
			equal = NULL;
		} else {  		// no equal -> no value
			if (equal == str)  {
				board_print(b, stderr);
				die("josekifix: bad variable, empty name: '%s' (run with -d5 to see previous moves)\n", str);
			}
			*equal = 0;
		}

		vars[k].name = str;
		if (!name_only)
			vars[k].value = equal + 1;
		
		// fprintf(stderr, "  name:  '%s'\n", vars[k].name);		
		// fprintf(stderr, "  value:  '%s'\n", vars[k].value);

		if (!*end)  break;    // last var
		*end = 0;
		str = end + 1;
	}

	// fprintf(stderr, "\n");
	*vars_len = k + 1;
	return vars;
}

/* ladder_own_setup_own    coord1 [coord2 ...]		setup stones for ladder_own ladder check (own stones)
 * ladder_own_setup_other  coord1 [coord2 ...]		setup stones for ladder_own ladder check (opponent stones) */
static void
parse_ladder_setup(char **override_ladder_setup, char *value)
{
	char *s = value;
	for (int i = 0; s && *s; i++) {
		override_ladder_setup[i] = s;
		char *spc = s = strchr(s, ' ');
		if (spc) {
			*spc = 0;
			s = spc + 1;
		}
	}	
}

typedef struct {
	int quadrants[4];   /* quadrant(s) in the order they were given (if present) */
	int n;
} external_engine_setting_t;

/* external_engine			enable external engine mode in current quadrant
 * external_engine = q1 [q2 ...]	enable external engine mode in given quadrants (numeric)   */
static void
parse_external_engine(board_t *b, override_t *override, external_engine_setting_t *setting, char *value)
{
	char *name = (override->name ? override->name : "");
	
        /* no value = current quadrant */
        if (!value || !value[0]) {
		int q = last_quadrant(b);
                override->external_engine_mode[q] = DEFAULT_EXTERNAL_ENGINE_MOVES;
		setting->quadrants[setting->n++] = q;
                return;
        }

        char *s = value;
        for (int i = 0; s && *s; i++) {
                int q = atoi(s);
                if (!isdigit(*s) || q < 0 || q > 3)
                        die("josekifix: \"%s\": bad external_engine value '%s', quadrants must be 0, 1, 2 or 3\n",
                            name, value);

                override->external_engine_mode[q] = DEFAULT_EXTERNAL_ENGINE_MOVES;

		/* Remember quadrants order for 'external_engine_moves' */
		setting->quadrants[setting->n++] = q;

                char *spc = s = strchr(s, ' ');
                if (spc)
                        s = spc + 1;
        }
}

/* external_engine_moves = n			specify number of moves for external engine mode
 * external_engine_moves = n1 n2 [...]		same for each quadrant if multiple quadrants have been enabled */
static void
parse_external_engine_moves(board_t *b, override_t *override, external_engine_setting_t *setting, char *value)
{
	assert(value && value[0]);
	char *name = (override->name ? override->name : "");

	int values[4];
	int n = 0;
	
	char *s = value;
	int q;
	for (q = 0; s && *s; q++) {
		int moves = atoi(s);
		if (!isdigit(*s) || moves <= 0)
			die("josekifix: \"%s\": bad external_engine_moves value '%s'\n", name, value);
		if (moves >= 80)
			fprintf(stderr, "josekifix: \"%s\": warning, really high number of external engine moves given: %i\n",
				name, moves);
		if (n >= 4)
			die("josekifix: \"%s\": too many values for external_engine_moves (4 max)\n", name);
		
		values[n++] = moves;

		char *spc = s = strchr(s, ' ');
		if (spc)
			s = spc + 1;
	}

	/* One value given: use that for all enabled quadrants */
	if (n == 1) {
		if (!setting->n) {
			override->external_engine_mode[last_quadrant(b)] = values[0];
			return;
		}
		
		for (int i = 0; i < setting->n; i++)
			override->external_engine_mode[setting->quadrants[i]] = values[0];
		return;
	}

	/* Multiple values given: must match previous 'external_engine_mode' setting */
	if (!setting->n)
		die("josekifix: \"%s\": 'external_engine_moves' needs a corresponding 'external_engine' setting.\n", name);
	if (n != setting->n)
		die("josekifix: \"%s\": 'external_engine_moves' and 'external_engine' must specify same number of quadrants.\n", name);
	assert(n == setting->n);
	
	for (int i = 0; i < n; i++)
		override->external_engine_mode[setting->quadrants[i]] = values[i];
}

/* Set override around coord */
static void
joseki_override_set_around(override_t *override, board_t *b, char *value)
{	
	assert(valid_coord(value));

	enum stone own_color = board_to_play(b);
	enum stone other_color = stone_other(own_color);
	coord_t c = str2coord(value);
	if      (board_at(b, c) == own_color)	 override->coord_own = value;
	else if (board_at(b, c) == other_color)	 override->coord_other = value;
	else					 override->coord_empty = value;
}

/* Parse and add override.
 * Help locate bad override if something goes wrong. */
static void
add_override(board_t *b, move_t *m, char *move_str)
{
	move_str = strdup(move_str);
	
	int vars_len;
	var_t *vars = parse_josekifix_vars(b, move_str, &vars_len);
	char *section = move_str;
	
	if (strcmp(section, "override") && strcmp(section, "log")) {
		board_print(b, stderr);
		die("josekifix: unknown section '%s', aborting. (run with -d5 to see previous moves)\n", section);
	}

	if (!b->moves)
		die("josekifix: can't add an override on empty board.\n");

	if (!vars_len) {  free(move_str);  return;  }

	// dump vars
	// for (int i = 0; i < vars_len; i++)
	//	if (vars[i].value)  fprintf(stderr, "  var:  '%s' = '%s'\n", vars[i].name, vars[i].value);
	//	else                fprintf(stderr, "  var:  '%s'\n", vars[i].name);

	// Main override
	override_t override  = { 0, };
	ladder_check_t *ladder_check = &override.ladder_check;
	ladder_check_t *ladder_check2 = &override.ladder_check2;
	bool has_around = false;

	// Second area check ?
	override_t override2 = { 0, };
	char *around2 = NULL;

	external_engine_setting_t setting;  setting.n = 0;

	char *override_name = "";
	for (int i = 0; i < vars_len; i++) {
		char *name  = vars[i].name;
		char *value = vars[i].value;

		/* name = override_name */
		if      (!strcmp(name, "name"))
			override.name = override_name = value;

		/* around = coord		match pattern origin.
		 * around = last		(use last move) */
		else if (!strcmp(name, "around")) {
			if (strcmp(value, "last") && !valid_coord(value)) {
				board_print(b, stderr);
				die("josekifix: \"%s\": invalid around coord '%s', aborting. (run with -d5 to see previous moves)\n",
				    override_name, value);
			}

			has_around = true;
			
			if (strcmp(value, "last"))
				joseki_override_set_around(&override, b, value);
		}

		/* around2 = coord		also check pattern at this location */
		else if (!strcmp(name, "around2")) {	// second area check
			if (strcmp(value, "last") && !valid_coord(value)) {
				board_print(b, stderr);
				die("josekifix: \"%s\": invalid around2 coord '%s', aborting. (run with -d5 to see previous moves)\n",
				    override_name, value);
			}
			
			around2 = value;		// deal with it later
		}
		
		/************************************************************************************/
		/* First ladder check */

		/* ladder_own = coord		ladder works for us at given coord (we atari) */
		else if (!strcmp(name, "ladder_own")) {
			ladder_check->own_color = true;
			ladder_check->coord = value;
			ladder_check->works = true;
		}
		/* ladder_own_setup_own    coord1 [coord2 ...]		setup stones for ladder_own ladder check (own stones)
		 * ladder_own_setup_other  coord1 [coord2 ...]		setup stones for ladder_own ladder check (opponent stones) */
		else if (!strcmp(name, "ladder_own_setup_own"))		parse_ladder_setup(ladder_check->setup_own, value);
		else if (!strcmp(name, "ladder_own_setup_other"))	parse_ladder_setup(ladder_check->setup_other, value);

		/* noladder_own = coord		ladder doesn't works for us at given coord (we atari) */
		else if (!strcmp(name, "noladder_own")) {
			ladder_check->own_color = true;
			ladder_check->coord = value;
			ladder_check->works = false;
		}
		else if (!strcmp(name, "noladder_own_setup_own"))	parse_ladder_setup(ladder_check->setup_own, value);
		else if (!strcmp(name, "noladder_own_setup_other"))	parse_ladder_setup(ladder_check->setup_other, value);

		/* ladder_other = coord		ladder works for opponent at given coord (he ataris) */
		else if (!strcmp(name, "ladder_other")) {
			ladder_check->own_color = false;
			ladder_check->coord = value;
			ladder_check->works = true;
		}
		else if (!strcmp(name, "ladder_other_setup_own"))	parse_ladder_setup(ladder_check->setup_own, value);
		else if (!strcmp(name, "ladder_other_setup_other"))	parse_ladder_setup(ladder_check->setup_other, value);

		/* noladder_other = coord	ladder doesn't works for opponent at given coord (he ataris) */
		else if (!strcmp(name, "noladder_other")) {
			ladder_check->own_color = false;
			ladder_check->coord = value;
			ladder_check->works = false;
		}
		else if (!strcmp(name, "noladder_other_setup_own"))	parse_ladder_setup(ladder_check->setup_own, value);
		else if (!strcmp(name, "noladder_other_setup_other"))	parse_ladder_setup(ladder_check->setup_other, value);
		
		
		/************************************************************************************/
		/* Second ladder check */

		/* ladder_own2 = coord		ladder works for us at given coord (we atari) */
		else if (!strcmp(name, "ladder_own2")) {
			ladder_check2->own_color = true;
			ladder_check2->coord = value;
			ladder_check2->works = true;
		}
		else if (!strcmp(name, "ladder_own2_setup_own"))	parse_ladder_setup(ladder_check2->setup_own, value);
		else if (!strcmp(name, "ladder_own2_setup_other"))	parse_ladder_setup(ladder_check2->setup_other, value);

		/* noladder_own2 = coord	ladder doesn't works for us at given coord (we atari) */
		else if (!strcmp(name, "noladder_own2")) {
			ladder_check2->own_color = true;
			ladder_check2->coord = value;
			ladder_check2->works = false;
		}
		else if (!strcmp(name, "noladder_own2_setup_own"))	parse_ladder_setup(ladder_check2->setup_own, value);
		else if (!strcmp(name, "noladder_own2_setup_other"))	parse_ladder_setup(ladder_check2->setup_other, value);

		/* ladder_other2 = coord	ladder works for opponent at given coord (he ataris) */
		else if (!strcmp(name, "ladder_other2")) {
			ladder_check2->own_color = false;
			ladder_check2->coord = value;
			ladder_check2->works = true;
		}
		else if (!strcmp(name, "ladder_other2_setup_own"))	parse_ladder_setup(ladder_check2->setup_own, value);
		else if (!strcmp(name, "ladder_other2_setup_other"))	parse_ladder_setup(ladder_check2->setup_other, value);

		/* noladder_other2 = coord	ladder doesn't works for opponent at given coord (he ataris) */
		else if (!strcmp(name, "noladder_other2")) {
			ladder_check2->own_color = false;
			ladder_check2->coord = value;
			ladder_check2->works = false;
		}
		else if (!strcmp(name, "noladder_other2_setup_own"))	parse_ladder_setup(ladder_check2->setup_own, value);
		else if (!strcmp(name, "noladder_other2_setup_other"))	parse_ladder_setup(ladder_check2->setup_other, value);
		
		
		/************************************************************************************/		

		/* see above */
		else if (!strcmp(name, "external_engine"))
			parse_external_engine(b, &override, &setting, value);

		/* external_engine_diag		enable external engine mode in opposite quadrant */
		else if (!strcmp(name, "external_engine_diag")) {
			int q = diag_quadrant(last_quadrant(b));
			override.external_engine_mode[q] = DEFAULT_EXTERNAL_ENGINE_MOVES;
			setting.quadrants[setting.n++] = q;
		}

		/* see above */
		else if (!strcmp(name, "external_engine_moves"))
			parse_external_engine_moves(b, &override, &setting, value);
		
		else {
			board_print(b, stderr);
			die("josekifix: \"%s\": unknown josekifix variable: '%s', aborting. (run with -d5 to see previous moves)\n",
			    override_name, name);
		}
	}
	
	override.prev = strdup(coord2sstr(last_move(b).coord));  // XXX switch to coord_t !
	override.next = strdup(coord2sstr(m->coord));

	// fill hashes
	joseki_override_fill_hashes(&override, b);
	
	if (around2) {  /* second area check */
		override2 = override;
		override2.coord_own = override2.coord_other = override2.coord_empty = 0;
		
		if (strcmp(around2, "last"))
			joseki_override_set_around(&override2, b, around2);

		// fill hashes
		joseki_override_fill_hashes(&override2, b);
	}

	if (DEBUGL(3)) {	/* display position and override */
		if (!DEBUGL(4))  board_print(b, stderr);
		char *title = (!strcmp(section, "override") ? "joseki override" : "joseki log");
		joseki_override_print(&override, title);
		if (around2)	/* <and> check */
			joseki_override_print(&override2, "and");
		fprintf(stderr, "\n");
	}

	/* All entries must have 'around' set */
	if (!has_around) {
		board_print(b, stderr);
		die("josekifix: \"%s\": around coord missing, aborting. (run with -d5 to see previous moves)\n",
		    override_name);
	}

	if (!strcmp(section, "override")) {
		if (around2)  josekifix_add_override_and(b, &override, &override2);
		else	      josekifix_add_override(b, &override);		
	}
	if (!strcmp(section, "log")) {
		if (around2)  josekifix_add_logged_variation_and(b, &override, &override2);
		else	      josekifix_add_logged_variation(b, &override);
	}
}


/**********************************************************************************************************/
/* JosekifixLoad engine */

/* Record joseki overrides found in the GTP stream (extra data after play commands)
 * Debugging: to dump all overrides, run                      'pachi -d4'
 *            to dump all overrides + earlier positions, run  'pachi -d5'  */
static char *
josekifixload_notify_play(engine_t *e, board_t *b, move_t *m, char *move_str, bool *printed_board)
{
	DEBUG_QUIET_END();	/* debugging hack (re-enable debug msg just here) */
	chomp(move_str);	/* XXX should be done by gtp layer ! */

	assert(!is_resign(m->coord));
	if (!b->moves)		/* New game */
		assert(board_rsize(b) == 19);

	if (DEBUGL(4))		/* display all positions (including early ones leading to overrides) */
		board_print(b, stderr);
	
	if (*move_str)
		add_override(b, m, move_str);

	DEBUG_QUIET();		/* quiet again */
	return NULL;
}

static coord_t
josekifixload_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	die("genmove command not available in josekifixload engine!\n");
}

static void
josekifixload_state_init(engine_t *e)
{
	options_t *options = &e->options;

	for (int i = 0; i < options->n; i++) {
		const char *optname = options->o[i].name;
		//const char *optval = options->o[i].val;

		die("josekifixload: Invalid engine argument %s or missing value\n", optname);
	}
}

void
josekifixload_engine_init(engine_t *e, board_t *b)
{
	josekifixload_state_init(e);
	e->name = "JosekifixLoad";
	e->comment = "You cannot play Pachi with this engine, it is intended for internal use (loading josekifix data)";
	e->genmove = josekifixload_genmove;
	e->notify_play = josekifixload_notify_play;
	
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;
}
