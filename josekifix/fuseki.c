#include <assert.h>
#include <stdarg.h>

#include "engine.h"
#include "pattern/spatial.h"
#include "josekifix/josekifix.h"
#include "tactics/util.h"

typedef coord_t (*override_hook_t)(struct board *b, hash_t lasth);

typedef struct {
	override_hook_t override;
	char *name;
} fuseki_t;


/**********************************************************************************************************/
/* Wild initial fusekis */

#define coord(str)  (str2coord(str))
#define empty(str)  (board_at(b, coord(str)) == S_NONE)
#define white(str)  (board_at(b, coord(str)) == S_WHITE)
#define hash_empty(str)  (empty(str) ? outer_spatial_hash_from_board(b, coord(str), last_move(b).color) : 0)
#define hash_white(str)  (white(str) ? outer_spatial_hash_from_board(b, coord(str), last_move(b).color) : 0)

#define random_coord(...)  get_random_coord(b, __VA_ARGS__, NULL)

/* Last move attachment or close ? */
static bool
just_approached(struct board *b)
{
	coord_t last = last_move(b).coord;
	if (last != pass && board_stone_radar(b, last, 2))
		return true;	
	return false;
}

static coord_t
get_random_coord(struct board *b, char *first, ...)
{
	if (!first) return pass;	
	va_list ap;  va_start(ap, first);
	
	int n = 0;
	char *coords[BOARD_MAX_COORDS];
	coords[n++] = first;
	char *str;
	while ((str = va_arg(ap, char*)))
		coords[n++] = str;
	va_end(ap);
	
	int i = fast_random(100);
	return coord(coords[i * n / 100]);
}

static coord_t
great_wall_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0:  return coord("K10");  // Tengen
		case 2:  return (hash_empty("K18") == 0xaf1ca7d9bc6ee27b ? coord("K16") : pass);
		case 4:  return (hash_empty("K2")  == 0x4a56cdcaed2d35eb ? coord("K4")  : pass);
		case 6:  return (hash_empty("K12") == 0xd696c67a8e541c9f ? coord("L13") : pass);
		case 8:  return (hash_empty("K8")  == 0xf389fa404333ddc7 ? coord("J7")  : pass);
		default: return pass;
	}
}

static coord_t
great_cross_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0:  return coord("K10");  // Tengen
		case 2:  return (hash_empty("K15") == 0x52940f053f7d41d8 ? coord("K16") : pass);
		case 4:  return (hash_empty("K5")  == 0xd3041f9087051224 ? coord("K4")  : pass);

		case 6:  return (hash_empty("E10") == 0x4b3a2de37f1672b0 ? coord("D10") : pass);
		case 8:  return (hash_empty("P10") == 0x1a311a3e8d8dc68c ? coord("Q10") : pass);
		default: return pass;
	}
}

static coord_t
tengen_sanrensei_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0: return coord("Q16");
		case 2: if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("Q4");
			if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("D16");
			return pass;
		case 4:
			if (hash_empty("K10") == 0x4e62eb7437da8b53)  return coord("K10");
			return pass;
		default: return pass;
	}
}

#if 0
static coord_t
five_five_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0: return coord("P15");
		case 2: if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("P5");
			if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("E15");
			return pass;
		case 4:
			if (hash_white("D4") == 0xfbbb40c10212374b)
				return random_coord("F3", "F3", "F4", "G3", "K4");
			return pass;
		default: return pass;
	}
}
#endif


static coord_t
double_takamoku_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0: return coord("Q15");
		case 2: if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("P4");
			if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("E16");
			return pass;
	}

	/* Cover invasion, otherwise no fun ... */
	override_t overrides[] = {
		{ .coord_empty = "Q5", .prev = "R4", "Q6", "",  { 0xb40892614d827e6, 0xbb42499bcc8ef68a, 0x7f3874ee2d7548a2, 0xfc3dfb8271de3b66,
								  0xa5f0ba7f0edf4c02, 0x613a14799996cc56, 0xed437c981690dc16, 0x1a8e9d4f0524feea } },
		{ NULL, NULL, NULL }
	};

	return check_overrides(b, overrides, lasth);
}

#if 0
// wild but not too good ...
static coord_t
christmas_island_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0: return coord("R14");
		case 2: if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("F17");
			if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("O3");
			return pass;
		default: return pass;
	}
}
#endif

static fuseki_t wild_fusekis[] = {
	{ great_wall_fuseki, "great wall" },
	{ great_cross_fuseki, "great cross" },
	{ tengen_sanrensei_fuseki, "tengen sanrensei" },
	//  { five_five_fuseki, "5-5" },
	{ double_takamoku_fuseki, "double takamoku" },
	//{ christmas_island_fuseki, "christmas island" },
	{ NULL, NULL }
};


/**********************************************************************************************************/
/* Regular initial fusekis */

static coord_t
large_keima_fuseki(struct board *b, hash_t lasth)
{
	override_t override = { .coord_empty = "P4", .prev = "", "O3", "",  { 0x77980cd3dd9328ef, 0x746b3bf60920fbc7, 0x66dfa042cb1f17cf, 0x652c97671facc4e7,
									       0x32c2c9f9bfa6523f, 0x42e8884f4f56b037, 0x21066ae947c2613f, 0x512c2b5fb7328337 } };

	switch (b->moves) {
		case 0: return coord("Q16");
		case 2: if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("R4");
			if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("D17");
			return pass;
		case 4: return check_override(b, &override, NULL, lasth);
		default: return pass;
	}
}

static coord_t
sanrensei_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0: return coord("Q16");
		case 2: if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("Q4");
			if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("D16");
			return pass;
		case 4: if (last_move2(b).coord == coord("Q4"))
				return (hash_empty("P10") == 0x6824b58429db8cde ? coord("Q10") : pass);
			if (last_move2(b).coord == coord("D16"))
				return (hash_empty("K15") == 0x1f77eaacf1573066 ? coord("K16") : pass);
		default: return pass;
	}
}

static coord_t
chinese_fuseki(struct board *b, hash_t lasth)
{
	switch (b->moves) {
		case 0: return coord("Q16");
		case 2: if (hash_empty("Q5")  == 0x4ff209de037e7964)  return coord("Q3");
			if (hash_empty("D15") == 0xf38ceba436dc80e4)  return coord("C16");
			return pass;
		case 4: if (last_move2(b).coord == coord("Q3"))
				return (hash_empty("P10") == 0x6824b58429db8cde ? random_coord("R9", "Q9") : pass);
			if (last_move2(b).coord == coord("C16"))
				return (hash_empty("K15") == 0x1f77eaacf1573066 ? random_coord("J17", "J16") : pass);
		default: return pass;
	}
}

static fuseki_t regular_fusekis[] = {
	{ large_keima_fuseki, "large keima" },
	{ sanrensei_fuseki, "sanrensei" },
	{ chinese_fuseki, "chinese" },
	{ NULL, NULL }
};


/**********************************************************************************************************/
/* Choose initial fuseki */

/* Proportion of wild fuseki games. */
static int wild_fuseki_rate = 0;

/* For regular games, proportion of games left untouched.  */
static int no_fuseki_rate = 25;


static fuseki_t *fuseki_handler = NULL;
static void reset_fuseki_handler()  {  fuseki_handler = NULL;  }

static fuseki_t*
get_fuseki_handler(struct board *b)
{
	if (b->handicap || b->moves > 100 || just_approached(b)) {  reset_fuseki_handler(); return NULL;  }

	if (b->moves)  return fuseki_handler;

	/* First move, pick one if needed ...  */
	
	fuseki_handler = NULL;
	fuseki_t *fusekis = regular_fusekis;
	if (wild_fuseki_rate > fast_random(100))
		fusekis = wild_fusekis;
	else    /* Regular or no fuseki. */
		if (no_fuseki_rate > fast_random(100))	return NULL;
	
	int n = 0;  while (fusekis[n].override)  n++;
	int i = fast_random(100) * n / 100;
	fuseki_handler = &fusekis[i];
	
	return fuseki_handler;
}

static coord_t
check_special_fuseki(struct board *b, hash_t lasth) {
	fuseki_t *fuseki = get_fuseki_handler(b);
	//fuseki_t *fuseki = &special_fusekis[1];  // debugging

	if (!fuseki)  return pass;
	
	coord_t c = fuseki->override(b, lasth);
	if (is_pass(c) || !josekifix_sane_override(b, c, fuseki->name, -1)) {
		reset_fuseki_handler();
		return pass;
	}
	
	josekifix_log("fuseki_override: %s (%s) move %i\n", coord2sstr(c), fuseki->name, b->moves);
	return c;
}

/* Use more varied fusekis when playing as black */
coord_t
josekifix_initial_fuseki(struct board *b, strbuf_t *log, hash_t lasth)
{
	coord_t c = pass;
	
	/* Special fuseki ? */
	c = check_special_fuseki(b, lasth);
	if (!is_pass(c))  return c;
	
	/* Rarely it plays something wild on empty board ... */
	if (!b->moves)  return coord("Q16");
	
	/* Move 3, make it more random ... */
	if (b->moves == 2 && !b->handicap && last_move(b).coord == coord("D4") &&
	    empty("Q3") && empty("Q4") && empty("R4"))
		return random_coord("Q3", "Q4", "R4");

	return pass;
}


