#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "tactics/util.h"
#include "pattern/spatial.h"
#include "josekifix/josekifixload.h"
#include "josekifix/josekifix.h"


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
	assert(valid_str_coord(value));

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
			if (strcmp(value, "last") && !valid_str_coord(value)) {
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
			if (strcmp(value, "last") && !valid_str_coord(value)) {
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
