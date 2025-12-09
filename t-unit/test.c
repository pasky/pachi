#define DEBUG
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "board.h"
#include "debug.h"
#include "pachi.h"
#include "tactics/selfatari.h"
#include "tactics/dragon.h"
#include "tactics/ladder.h"
#include "tactics/1lib.h"
#include "tactics/2lib.h"
#include "tactics/seki.h"
#include "tactics/util.h"
#include "util.h"
#include "random.h"
#include "playout.h"
#include "timeinfo.h"
#include "ownermap.h"
#include "playout/moggy.h"
#include "engines/replay.h"
#include "uct/internal.h"
#include "dcnn/dcnn.h"


/* Running tests over gtp ? */
static bool tunit_over_gtp = 1;
static bool board_printed;
static bool last_move_set;
static char *current_cmd = NULL;
static char *next = NULL;

/* Get next argument (must be there) */
#define next_arg(to_)   do { \
	to_ = next; \
	if (!*(to_))  die("%s: argument missing\n", current_cmd); \
	next += strcspn(next, " \t"); \
	if (*next) { \
		*next = 0; next++; \
		next += strspn(next, " \t"); \
	} \
} while (0)

/* Get next argument (optional) */
#define next_arg_opt(to_)   do { \
	to_ = next; \
	next += strcspn(next, " \t"); \
	if (*next) { \
		*next = 0; next++; \
		next += strspn(next, " \t"); \
	} \
} while (0)

/* save current command and skip to first argument
 * before calling function */
static void
init_arg(char *line)
{
	current_cmd = next = line;
	char *arg;
	next_arg(arg);	
}

static void
init_arg_len(char *line, int len)
{
	current_cmd = next = line;
	next = line + len - 1;
	char *arg;
	next_arg(arg);
}

/* Check no more args */
static void
args_end()
{
	if (*next)  die("Invalid extra arg: '%s'\n", next);
}

static void
remove_comments(char *line)
{
	if (strchr(line, '#'))
		*strchr(line, '#') = 0;
}

static void
board_print_test(board_t *b)
{
	if (!DEBUGL(2) || board_printed)
		return;
	board_print(b, stderr);
	board_printed = true;
}

static void
engine_board_print_test(engine_t *e, board_t *b)
{
	if (!DEBUGL(2) || board_printed)
		return;
	engine_board_print(e, b, stderr);
	board_printed = true;
}

static void
check_play_move(board_t *b, move_t *m)
{
	if (board_play(b, m) < 0) {
		fprintf(stderr, "Failed to play %s %s\n", stone2str(m->color), coord2sstr(m->coord));
		board_print(b, stderr);
		exit(EXIT_FAILURE);
	}
}

static void
set_komi(board_t *b, char *arg)
{
	next_arg(arg);
	assert(*arg == '-' || *arg == '+' || isdigit(*arg));
	b->komi = atof(arg);
}

static void
set_rules(board_t *b, char *arg)
{
	next_arg(arg);
	bool r = board_set_rules(b, arg);
	if (!r)  die("bad rules: %s\n", arg);
}

static void
set_passes(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	assert(color != S_NONE);
	next_arg(arg);
	assert(isdigit(*arg));
	int n = atoi(arg);
	b->passes[color] += n;
}

static void
set_handicap(board_t *b, char *arg)
{
	next_arg(arg);	
	assert(isdigit(*arg));
	b->handicap = atoi(arg);
}

static void
board_load(board_t *b, FILE *f, char *arg)
{
	next_arg(arg);
	assert(isdigit(*arg));
	int size = atoi(arg);
	move_t last_move = move(pass, S_NONE);
	last_move_set = false;
	board_resize(b, size);
	b->rules = RULES_CHINESE;  /* reset rules in case they got changed */
	board_clear(b);
	for (int y = size - 1; y >= 0; y--) {
		char line[256];
		if (!fgets(line, sizeof(line), f))  die("Premature EOF.\n");
		chomp(line);
		remove_comments(line);

		char *cmd = line;
		if ('a' <= line[0] && line[0] <= 'z')  init_arg(line);
		if (!strcmp("komi", cmd))     {  set_komi(b, next);     y++; continue;  }
		if (!strcmp("rules", cmd))    {  set_rules(b, next);    y++; continue;  }
		if (!strcmp("passes", cmd))   {  set_passes(b, next);   y++; continue;  }
		if (!strcmp("handicap", cmd)) {  set_handicap(b, next); y++; continue;  }

		if ((int)strlen(line) != size * 2 - 1 && 
		    (int)strlen(line) != size * 2)       die("Line not %d char long: '%s'\n", size * 2 - 1, line);
		
		for (int i = 0; i < size * 2; i++) {
			enum stone s;
			switch (line[i]) {
				case '.': s = S_NONE; break;
				case 'X': s = S_BLACK; break;
				case 'O': s = S_WHITE; break;
				default : die("Invalid stone '%c'\n", line[i]);
			}
			i++;
			if (line[i] && line[i] != ' ' && line[i] != ')')
				die("No space after stone %i: '%c'\n", i/2 + 1, line[i]);

			move_t m = move(coord_xy(i/2 + 1, y + 1), s);
			if (line[i] == ')') {
				assert(s == S_BLACK || s == S_WHITE);
				assert(last_move.coord == pass);
				last_move = m;
				last_move_set = true;
				continue;	/* Play last move last ... */
			}

			if (s == S_NONE) continue;

			check_play_move(b, &m);
		}
	}
	if (last_move.coord != pass)
		check_play_move(b, &last_move);
	int suicides = b->captures[S_BLACK] || b->captures[S_WHITE];
	assert(!suicides);
}

static void
set_ko(board_t *b, char *arg)
{
	next_arg(arg);
	assert(isalpha(*arg));
	move_t last;
	last.coord = str2coord(arg);
	last.color = board_at(b, last.coord);
	assert(last.color == S_BLACK || last.color == S_WHITE);
	last_move(b) = last;

	/* Sanity checks */
	group_t g = group_at(b, last.coord);
	assert(board_group_info(b, g).libs == 1);
	assert(group_stone_count(b, g, 2) == 1);
	coord_t lib = board_group_info(b, g).lib[0];
	assert(board_is_eyelike(b, lib, last.color));

	b->ko.coord = lib;
	b->ko.color = stone_other(last.color);
}

static int optional = 0;
static char title[256];

static void
show_title_if_needed()
{
	if (debug_level == 1 && *title) {
		fprintf(stderr, "\n%s\n", title);
		*title = 0;
	}
}

#define PRINT_TEST(board, format, ...)	do {			   \
	board_print_test(board);				   \
	if (DEBUGL(1))  fprintf(stderr, format, __VA_ARGS__);	   \
} while(0)

/* Print test result (passed / failed) */
#define PRINT_RES()  do {					\
	if (rres != eres) {					\
		show_title_if_needed();				\
		if (DEBUGL(0))  fprintf(stderr, "FAILED %s\n", (optional ? "(optional)" : "")); \
	} else								\
		if (DEBUGL(1))  fprintf(stderr, "OK\n");		\
} while(0)

/* Print test result, show returned value if it fails */
#define PRINT_RES_VAL(format, value)  do {			\
	if (rres != eres) {					\
		show_title_if_needed();				\
		if (DEBUGL(0))  fprintf(stderr, "got " format "  FAILED %s\n", value, (optional ? "(optional)" : "")); \
	} else								\
		if (DEBUGL(1))  fprintf(stderr, "OK\n");		\
} while(0)


static bool
test_bad_selfatari(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "bad_selfatari %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = is_bad_selfatari(b, color, c);

	PRINT_RES();
	return   (rres == eres);
}

static bool
test_really_bad_selfatari(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "really_bad_selfatari %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = is_really_bad_selfatari(b, color, c);

	PRINT_RES();
	return   (rres == eres);
}

static bool
test_3lib_selfatari(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "3lib_selfatari %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);

	/* Sanity check: 3lib group nearby */
	foreach_neighbor(b, c, {
		if (board_at(b, c) != color)  continue;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs == 3)
			goto good;
	});
	die("no 3lib group near %s %s, bad test ?\n", stone2str(color), coord2sstr(c));

 good:;
	int rres = is_3lib_selfatari(b, color, c);

	PRINT_RES();
	return   (rres == eres);
}

static bool
test_snapback(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "snapback %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = is_snapback(b, color, c, NULL);

	PRINT_RES();
	return   (rres == eres);
}

static bool
test_false_eye_seki(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "false_eye_seki %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = breaking_false_eye_seki(b, c, color);

	PRINT_RES();
	return   (rres == eres);
}

static bool
test_breaking_3_stone_seki(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "breaking_3_stone_seki %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = breaking_3_stone_seki(b, c, color);

	PRINT_RES();
	return   (rres == eres);
}

static bool
test_ladder(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "ladder %s %s %d...\t", stone2str(color), coord2sstr(c), eres);
	
	assert(board_at(b, c) == color);
	group_t group = group_at(b, c);
	assert(board_group_info(b, group).libs == 1);
	int rres = is_ladder(b, group, true);
	
	PRINT_RES();
	return   (rres == eres);
}

static bool
test_ladder_any(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "ladder_any %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == color);
	group_t group = group_at(b, c);
	assert(board_group_info(b, group).libs == 1);
	int rres = is_ladder_any(b, group, true);
	
	PRINT_RES();
	return   (rres == eres);
}

static bool
test_wouldbe_ladder(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "wouldbe_ladder %s %s %d...\t", stone2str(color), coord2sstr(c), eres);
	
	assert(board_at(b, c) == S_NONE);
	group_t g = board_get_2lib_neighbor(b, c, stone_other(color));
	assert(g); assert(board_at(b, g) == stone_other(color));
	coord_t chaselib = c;
	int rres = wouldbe_ladder(b, g, chaselib);
	
	PRINT_RES();
	return   (rres == eres);
}

static bool
test_wouldbe_ladder_any(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "wouldbe_ladder_any %s %s %d...\t", stone2str(color), coord2sstr(c), eres);
	
	assert(board_at(b, c) == S_NONE);
	group_t g = board_get_2lib_neighbor(b, c, stone_other(color));
	assert(g); assert(board_at(b, g) == stone_other(color));
	coord_t chaselib = c;
	int rres = wouldbe_ladder_any(b, g, chaselib);
	
	PRINT_RES();
	return   (rres == eres);
}

static bool
test_useful_ladder(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "useful_ladder %s %s %d...\t", stone2str(color), coord2sstr(c), eres);
	
	assert(board_at(b, c) == S_NONE);
	group_t atari_neighbor = board_get_atari_neighbor(b, c, color);
	assert(atari_neighbor);
	int ladder = is_ladder(b, atari_neighbor, true);  assert(ladder);
	int rres = useful_ladder(b, atari_neighbor);
	
	PRINT_RES();
	return   (rres == eres);
}

static bool
test_can_countercap(board_t *b, char *arg)
{
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "can_countercap %s %d...\t", coord2sstr(c), eres);

	enum stone color = board_at(b, c);
	group_t g = group_at(b, c);
	assert(color == S_BLACK || color == S_WHITE);
	int rres = can_countercapture(b, g, NULL);

	PRINT_RES();
	return   (rres == eres);
}

/* Test mm atari pattern. usage:
 *	atari color coord [!]atari_feature
 * For example:
 *      atari b d4 atari:snapback
 *      atari b d4 !atari:and_cap  
 *      atari b d4 -1               (no match) */
static bool
test_atari(board_t *b, char *arg)
{
	static int init = 0;
	if (!init) {	/* init feature info */
		pattern_config_t pc;
		patterns_init(&pc, NULL, false, true);
		init = 1;
	}
	
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	bool negated = (*arg == '!');
	if (negated)  arg++;
	int expected = -1;
	feature_t f;
	if (atoi(arg) != -1) {
		str2feature(arg, &f);
		assert(f.id == FEAT_ATARI);
		expected = f.payload;
	}
	args_end();

	ownermap_t ownermap;
	mcowner_playouts(b, color, &ownermap);
	board_print_ownermap(b, stderr, &ownermap);
	board_printed = true;

	if (expected == -1)  PRINT_TEST(b, "atari %s %-3s -1...\t", stone2str(color), coord2sstr(c));
	else                 PRINT_TEST(b, "atari %s %-3s %s%s...\t", stone2str(color), coord2sstr(c),
					(negated ? "!" : ""), feature2sstr(&f));
	
	assert(board_at(b, c) == S_NONE);
	with_move_strict(b, c, color, {
		assert(board_get_atari_neighbor(b, c, stone_other(color)) ||
		       board_at(b, c) == S_NONE);   // suicide
	});

	move_t m = move(c, color);
	int feature = pattern_match_atari(b, &m, &ownermap);
	
	int eres = true;
	int rres = (negated ? feature != expected : feature == expected);
	
	PRINT_RES();
	if (rres != eres && DEBUGL(1)) {
		f.payload = feature;
		if (rres == -1)  fprintf(stderr, "           got  -1\n");
		else             fprintf(stderr, "           got  %s\n", feature2sstr(&f));
	}
	return   (rres == eres);
}

static bool
test_two_eyes(board_t *b, char *arg)
{
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "two_eyes %s %d...\t", coord2sstr(c), eres);

	enum stone color = board_at(b, c);
	assert(color == S_BLACK || color == S_WHITE);
	int rres = dragon_is_safe(b, group_at(b, c), color);

	PRINT_RES();
	return   (rres == eres);
}


/* syntax: pass_is_safe color expected_result */
static bool
test_pass_is_safe(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	DEBUG_QUIET();
	engine_t *e = new_engine(E_UCT, "", b);
	uct_t *u = (uct_t*)(e->data);
	DEBUG_QUIET_END();

	/* Not exactly same as game conditions as we just run some playouts
	 * instead of full tree search, but should be good enough for testing. */

	// show board with ownermap
	engine_ownermap(e, b);
	engine_board_print_test(e, b);
	PRINT_TEST(b, "pass_is_safe %s ?\n", stone2str(color));
	
	char *msg;
	mq_t dead;
	uct_mcowner_playouts(u, b, color);
	memcpy(&u->initial_ownermap, &u->ownermap, sizeof(u->ownermap));
	int rres = uct_pass_is_safe(u, b, color, false, &dead, &msg, DEBUGL(2));

	if (DEBUGL(2) && rres)  {
		fprintf(stderr, "-> yes: final score %s  (%s)\n", board_official_score_str(b, &dead), rules2str(b->rules));
		board_print_official_ownermap(b, &dead);
	}
	if (DEBUGL(2) && !rres)  // show reason
		fprintf(stderr, "-> no:  %s\n", msg);
	
	PRINT_TEST(b, "pass_is_safe %s %d...\t", stone2str(color), eres);

	engine_done(e);
	
	PRINT_RES();
	return   (rres == eres);
}

/* syntax: final_score expected_result */
static bool
test_final_score(board_t *b, char *arg)
{
	next_arg(arg);
	assert(str_prefix("B+", arg) || str_prefix("W+", arg));
	assert(isdigit(arg[2]) || arg[2] == '.');
	float sign = (str_prefix("B+", arg) ? -1 : 1);
	float eres = sign * atof(arg + 2);
	args_end();

	engine_t *e = new_engine(E_UCT, "", b);
	mq_t dead;
	engine_dead_groups(e, b, &dead);
		
	float rres = board_official_score(b, &dead);

	board_print_official_ownermap(b, &dead);
	board_printed = true;
	PRINT_TEST(b, "final_score %s...\t", arg);

	engine_done(e);
	
	PRINT_RES_VAL("%s", board_official_score_str(b, &dead));
	return (rres == eres);
}

#define PLAYOUT_NBEST 20

enum moggy_move_test {
	MMT_NORMAL,
	MMT_ABOVE,
	MMT_BELOW,
	MMT_MAX
};

typedef struct {
	enum moggy_move_test type;
	bool not;
	mq_t q;
	int pc;
} moggy_move_test_t;

static void
print_moggy_move_test(moggy_move_test_t *test, int rank)
{
	if (rank != -1)
		fprintf(stderr, "%i=", rank + 1);
	if (test->not)
		fprintf(stderr, "!");

	mq_t *q = &test->q;
	if (q->moves >= 2) {
		fprintf(stderr, "(%s", coord2sstr(q->move[0]));
		for (int j = 1; j < q->moves; j++)
			fprintf(stderr, "|%s", coord2sstr(q->move[j]));
		fprintf(stderr, ")");
	} else {
		assert(q->moves == 1);
		fprintf(stderr, "%s", coord2sstr(q->move[0]));
	}

	if (test->type == MMT_ABOVE)
		fprintf(stderr, ">=%i", test->pc);
	if (test->type == MMT_BELOW)
		fprintf(stderr, "<=%i", test->pc);
	fprintf(stderr, " ");
}

static void
moggy_moves_print_tests(moggy_move_test_t *global, int nglobal, moggy_move_test_t *ranked, int max)
{
	fprintf(stderr, "moggy moves ");
	for (int i = 0; i < nglobal; i++)
		print_moggy_move_test(&global[i], -1);

	for (int i = 0; i < max; i++)
		if (ranked[i].q.moves)
			print_moggy_move_test(&ranked[i], i);
	fprintf(stderr, "  ");
}

static bool
moggy_move_test(moggy_move_test_t *test, coord_t c, float r)
{
	bool res = mq_has(&test->q, c);

	if (test->type == MMT_ABOVE)
		res = (res && r * 100 >= test->pc);
	if (test->type == MMT_BELOW)
		res = (res && r * 100 <= test->pc);
	if (test->not)
		res = !res;
	return res;
}

static void
moggy_moves_show_moves(coord_t *best_c, float *best_r, int nbest, moggy_move_test_t *ranked, int max)
{
	fprintf(stderr, "playout moves:\n");
	for (int i = 0; i < nbest && best_c[i]; i++) {
		fprintf(stderr, "  %3s %5.1f%%        ", coord2sstr(best_c[i]), best_r[i] * 100);
		/* Show ranked tests side by side */
		if (i < max && ranked[i].q.moves) {
			bool passed = moggy_move_test(&ranked[i], best_c[i], best_r[i]);
			print_moggy_move_test(&ranked[i], -1);
			fprintf(stderr, "%s", (passed ? "OK" : "FAILED"));
		}
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");
}

static bool
moggy_moves_check_result(coord_t *best_c, float *best_r,
			 moggy_move_test_t *global, int nglobal,
			 moggy_move_test_t *ranked, int max)
{
	/* Check results: global tests */
	for (int n = 0; n < nglobal; n++)
		if (!global[n].not) {  /* one match = good */
			bool passed = false;
			for (int i = 0; i < PLAYOUT_NBEST && best_c[i]; i++)
				if (moggy_move_test(&global[n], best_c[i], best_r[i]))
					passed = true;
			if (!passed)  return false;
		} else  /* must match all moves */
			for (int i = 0; i < PLAYOUT_NBEST && best_c[i]; i++)
				if (!moggy_move_test(&global[n], best_c[i], best_r[i]))
					return false;

	/* Check results: ranked tests */
	assert(max <= PLAYOUT_NBEST);
	for (int i = 0; i < max; i++) {
		if (!ranked[i].q.moves)
			continue;
		if (!best_c[i] ||	/* If we're missing moves to test that's a fail. */
		    !moggy_move_test(&ranked[i], best_c[i], best_r[i]))
			return false;
	}

	return true;
}

/* Sample moves played by moggy in a given position.
 * Board last move matters quite a lot and must be set.
 * 
 * Syntax:  moggy moves coord [...]			expect coord among played moves
 *          moggy moves !coord [...]			forbidden move
 *          moggy moves coord>60 [...]			expect coord, played >= 60%
 *          moggy moves coord<20 [...]			expect coord, played <= 20%
 *          moggy moves 1=coord [...]			expect best move #1 = coord
 *          moggy moves 1=!coord [...]			expect best move #1 != coord
 *          moggy moves 1=coord>60 [...]		expect best move #1 = coord, played at least 60%
 *          moggy moves 1=(coord1|coord2) [...]		expect best move #1 = coord1 or coord2
 *          moggy moves 1=coord1 2=coord2 [...]		expect best move #1 = coord1, #2 = coord2
 *          moggy moves					show playout best moves   */
static bool
test_moggy_moves(board_t *b, char *arg)
{
	next_arg_opt(arg);

	/* Tests that apply to all best moves */
	moggy_move_test_t global[10];  memset(global, 0, sizeof(global));
	int nglobal = 0;
	/* Tests that apply only to some best move rank(s) */
	moggy_move_test_t ranked[10];  memset(ranked, 0, sizeof(ranked));
	
	while (*arg) {
		assert(nglobal < 10);
		moggy_move_test_t *test = &global[nglobal++];
		
		if (isdigit(*arg)) {  /* Best move rank given */
			int i = atoi(arg) - 1;
			assert(i < 10 && i >= 0);
			test = &ranked[i];
			nglobal--;
			while (isdigit(*arg))
				arg++;
			assert(*arg == '=');  arg++;
		}

		if (*arg == '!') {
			test->not = true;
			arg++;
		}

		mq_t *q = &test->q;
		if (isalpha(*arg)) {  /* Single expected coord */
			coord_t c = str2coord(arg);
			arg += strlen(coord2sstr(c));
			assert(board_at(b, c) == S_NONE);
			mq_add(q, c);
		} else if (*arg == '(') {  /* Set of expected coords */
			arg++;
			while (*arg && *arg != ')') {
				if (!isalpha(*arg))  die("Invalid coord: '%s'\n", arg);
				coord_t c = str2coord(arg);
				assert(board_at(b, c) == S_NONE);
				mq_add(q, c);
				while (isalnum(*arg))
					arg++;
				assert(*arg == '|' || *arg == ')');
				if (*arg == '|')
					arg++;
			}
			assert(*arg == ')');  arg++;
		} else die("Invalid arg: '%s'\n", arg);

		if (*arg == '>' || *arg == '<') {
			test->type = (*arg == '>' ? MMT_ABOVE : MMT_BELOW);
			arg++;
			if (*arg == '=')  arg++;
			assert(isdigit(*arg));
			test->pc = atoi(arg);
			while (isdigit(*arg))
				arg++;
		}
		
		assert(!*arg);
		next_arg_opt(arg);
	}
	args_end();
	
	if (!tunit_over_gtp) assert(last_move_set);

	enum stone color = board_to_play(b);
	board_print_test(b);

	/* Get playout best moves */
	time_info_t ti = ti_none;
	engine_t e;  engine_init(&e, E_REPLAY, "", b);
	coord_t best_c[PLAYOUT_NBEST];
	float   best_r[PLAYOUT_NBEST];
	best_moves_setup(best, best_c, best_r, PLAYOUT_NBEST);
	engine_best_moves(&e, b, &ti, color, &best);

	/* Show playout moves */
	if (DEBUGL(2))  moggy_moves_show_moves(best_c, best_r, PLAYOUT_NBEST, ranked, 10);

	/* Print test */
	if (DEBUGL(1))  moggy_moves_print_tests(global, nglobal, ranked, 10);

	bool ret = moggy_moves_check_result(best_c, best_r, global, nglobal, ranked, 10);
	bool rres = ret, eres = true;
	PRINT_RES();

	engine_done(&e);
	return ret;
}

static int
moggy_games(board_t *b, enum stone color, int games, ownermap_t *ownermap, bool speed_benchmark)
{
	playout_policy_t *policy = playout_moggy_init(NULL, b);
	playout_setup_t setup = playout_setup(MAX_GAMELEN, 0);
	ownermap_init(ownermap);
	
	int wr = 0;
	double time_start = time_now();
	for (int i = 0; i < games; i++)  {
		board_t b2;
		board_copy(&b2, b);
		
		int score = playout_play_game(&setup, &b2, color, NULL, ownermap, policy);
		if (color == S_WHITE)
			score = -score;
		wr += (score > 0);
		board_done(&b2);
	}
	
	double elapsed = time_now() - time_start;
	if (DEBUGL(2) && speed_benchmark)
		fprintf(stderr, "moggy status in %.1fs, %i games/s\n\n", elapsed, (int)((float)games / elapsed));

	playout_policy_done(policy);
	return wr;
}

/* Play a number of moggy games, show ownermap and stats on final status of given coord(s)
 * Board last move matters quite a lot and must be set.
 *
 * Syntax:
 *   moggy status coord [x|o] [coord...]       coord owned by b/w  >= 67%
 *   moggy status coord [X|O] [coord...]       coord owned by b/w  >= 80%
 *   moggy status coord   :   [coord...]       coord dame          >= 67%
 *   moggy status coord   ?   [coord...]       just check status, test never fails
 *   moggy status                              speed benchmark
 */
static bool
test_moggy_status(board_t *b, char *arg)
{
	next_arg_opt(arg);

	bool speed_benchmark = !*arg;
	int games = (speed_benchmark ? 4000 : 500);
	coord_t              coords[10];
	enum point_judgement expected[10];
	int                  thres[10];
	int n = 0;
	
	for (n = 0; *arg; n++) {
		if (!isalpha(*arg))  die("Invalid arg: '%s'\n", arg);
		assert(n < 10);
		coords[n] = str2coord(arg);
		next_arg(arg);

		if (!*arg || strlen(arg) != 1) die("Expected x/o/X/O/: after coord %s\n", coord2sstr(coords[n]));
		thres[n] = (!strcmp(arg, "X") || !strcmp(arg, "O" ) ? 80 : 67);
		if      (!strcasecmp(arg, "x"))  expected[n] = PJ_BLACK;
		else if (!strcasecmp(arg, "o"))  expected[n] = PJ_WHITE;
		else if (!strcasecmp(arg, ":"))  expected[n] = PJ_SEKI;
		else if (!strcasecmp(arg, "?"))  { expected[n] = PJ_BLACK; thres[n] = 0;  }
		else    die("Expected x/o/X/O/: after coord %s\n", coord2sstr(coords[n]));
		next_arg_opt(arg);
	}
	args_end();

	if (!tunit_over_gtp) assert(last_move_set);
	
	enum stone color = board_to_play(b);
	board_print_test(b);
	if (DEBUGL(2)) {
		fprintf(stderr, "moggy status ");
		for (int i = 0; i < n; i++) {
			const char *chr = (thres[i] == 80 ? ":XO," : ":xo,");
			if (!thres[i])  chr = "????";
			fprintf(stderr, "%s %c  ", coord2sstr(coords[i]), chr[expected[i]]);
		}
		fprintf(stderr, "\n%s to play. Playing %i games ...\n", stone2str(color), games);
	}

	/* Get final status estimate after a number of moggy games */	
	ownermap_t ownermap;
	int wr = moggy_games(b, color, games, &ownermap, speed_benchmark);

	int wr_black = wr * 100 / games;
	int wr_white = (games - wr) * 100 / games;
	if (DEBUGL(2)) {
		if (wr_black > wr_white)  fprintf(stderr, "Winrate: [ black %i%% ]  white %i%%\n\n", wr_black, wr_white);
		else		          fprintf(stderr, "Winrate: black %i%%  [ white %i%% ]\n\n", wr_black, wr_white);
		board_print_ownermap(b, stderr, &ownermap);
	}

	/* Check results */
	bool ret = true;
	for (int i = 0; i < n; i++) {
		coord_t c = coords[i];
		enum point_judgement j = ownermap_judge_point(&ownermap, c, 0.8);
		if (j == PJ_UNKNOWN) j = ownermap_judge_point(&ownermap, c, 0.67);		
		enum stone color = (enum stone)j;
		if (j == PJ_UNKNOWN)
			color = (ownermap.map[c][S_BLACK] > ownermap.map[c][S_WHITE] ? S_BLACK : S_WHITE);
		int pc = ownermap.map[c][color] * 100 / ownermap.playouts;

		int passed = (!thres[i] || (j == expected[i] && pc >= thres[i]));
		const char *colorstr = (j == PJ_SEKI ? "seki" : stone2str(color));
		PRINT_TEST(b, "moggy status %3s:  %-5s %3i%%    ", coord2sstr(c), colorstr, pc);
		
		bool rres = passed, eres = true;
		PRINT_RES();
		if (!passed)  ret = false;
	}
	
	return ret;
}

/* Run playout showing board, candidate moves and playout logic behind
 * each move. Useful to visualize / debug / investigate what's going on
 * in playouts in a real game. Other tools give either candidate moves
 * (t-unit moggy moves) or playout logic (pachi -d6), here we get both.
 * playout.c and moggy.c must be compiled with debug messages enabled
 * (#define DEBUG) for this to work.
 * Not a full playout_play_game() replica at this stage (bent-four logic
 * missing for example) but should be pretty close. */
static bool
moggy_debug_game(board_t *board, char *arg)
{
	int prev_debug_level = debug_level;
	playout_policy_t *policy = playout_moggy_init(NULL, board);
	playout_setup_t setup = playout_setup(MAX_GAMELEN, 0);
	
	board_t b2;
	board_t *b = &b2;
	board_copy(b, board);

	engine_t e;  engine_init(&e, E_REPLAY, "", b);
	time_info_t ti = ti_none;
	
	board_print_test(b);
	fprintf(stderr, "moggy debug_game ...\n\n");
	
	/* playout_play_game() logic ... */
	
	b->playout_board = true;   // don't need board hash, history ...

	enum stone color = board_to_play(b);
	int gamelen = setup.gamelen - b->moves;
	int passes = is_pass(last_move(b).coord) && b->moves > 0;

	if (policy->setboard)
		policy->setboard(policy, b);

	/* Play until both sides pass, or we hit threshold. */
	while (gamelen-- > 0 && passes < 2) {
		/* Show best candidates */
		int nbest = 10;
		coord_t best_c[nbest];
		float   best_r[nbest];
		best_moves_setup(best, best_c, best_r, nbest);
		engine_best_moves(&e, b, &ti, color, &best);
		moggy_moves_show_moves(best_c, best_r, nbest, NULL, 0);

		/* Show chosen move and playout thinking logic */
		fprintf(stderr, "picked move:\n");
		debug_level = 6;
		coord_t coord = playout_play_move(&setup, b, color, policy);
		debug_level = prev_debug_level;
		fprintf(stderr, "\n");

		board_print(b, stderr);
		
		if (unlikely(is_pass(coord)))  passes++;
		else                           passes = 0;

		if (setup.mercymin && abs(b->captures[S_BLACK] - b->captures[S_WHITE]) > setup.mercymin)
			break;

		color = stone_other(color);
	}

	/* XXX playout_play_game() bent-four logic missing here */

	assert(b->rules == RULES_CHINESE);
	float score = board_fast_score(b);  /* From w perspective */
	fprintf(stderr, "Score: %s+%.1f\n", (score > 0 ? "W" : "B"), fabs(score));

	board_done(b);

	return true;
}

/* Test uct genmove on given position, make sure we get/don't get wanted/unwanted moves.
 * Although should be fine as t-unit test in most cases, to reproduce game conditions
 * exactly better use t-unit over gtp ('tunit genmove ...' cmd), t-unit board diagrams
 * don't preserve last 4 moves.
 *
 * Syntax:
 *   genmove color move                        expect move
 *   genmove color move1 move2 [...]           expect move1 or move2
 *   genmove color !move                       expect anything but move
 *   genmove color !move1 !move2 [...]         expect anything but move1 and move2    */
static bool
test_genmove(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);

	char* args[30];  int n;
	mq_t wanted;    mq_init(&wanted);
	mq_t unwanted;  mq_init(&unwanted);
	for (n = 0; *arg; n++) {
		args[n] = arg;
		mq_t *q = (*arg == '!' ? &unwanted : &wanted);
		if (*arg == '!')  arg++;
		if (!strcmp(arg, "pass"))   {  mq_add(q, pass);    next_arg_opt(arg);  continue;  }
		if (!strcmp(arg, "resign")) {  mq_add(q, resign);  next_arg_opt(arg);  continue;  }
		if (!valid_coord(arg))  die("Invalid move: '%s'\n", arg);
		mq_add(q, str2coord(arg));
		next_arg_opt(arg);
	}
	if (!wanted.moves && !unwanted.moves)  die("No moves specified");
	if (wanted.moves && unwanted.moves)    die("Can't have both wanted and unwanted moves");
	args_end();
	
	board_print_test(b);
	fprintf(stderr, "genmove %s ", stone2str(color));
	for (int i = 0; i < n; i++)
		fprintf(stderr, "%s ", args[i]);
	fprintf(stderr, "...\n\n");

	engine_t *e;
	if (tunit_over_gtp) {
		/* Use main engine. Creating new engine messes up context which is important here. */
		e = pachi_main_engine();
	} else
		e = new_engine(E_UCT, "", b);

	static time_info_t ti = { 0, };
	if (!time_parse(&ti, "=5000:10000"))  die("shouldn't happen");
	coord_t c = e->genmove(e, b, &ti, color, false);

	board_t b2;  board_copy(&b2, b);
	if (c != resign) {
		move_t m = move(c, color);
		if (board_play(&b2, &m) < 0)  die("invalid move, shouldn't happen");
	}
	engine_board_print(e, &b2, stderr);
	
	bool eres = true;
	bool rres = (wanted.moves ? false : true);
	for (int i = 0; i < n; i++)
		if (c == wanted.move[i])
			rres = true;
	for (int i = 0; i < n; i++)
		if (c == unwanted.move[i])
			rres = false;

	fprintf(stderr, "\n");
	PRINT_RES_VAL("%s", coord2sstr(c));

	if (!tunit_over_gtp)
		engine_done(e);

	return rres;
}


#ifdef DCNN

/* Use fake values for dcnn blunder testing (fast + ensures all moves are tested) */
#define FAKE_DCNN_OUTPUT  1

/* Test dcnn blunders on given position, make sure we get/don't get wanted/unwanted moves.
 *   dcnn_blunder         color coords   ->  check killed moves
 *   dcnn_blunder boosted color coords   ->  check boosted moves
 * Better use t-unit over gtp to reproduce game conditions exactly (see test_genmove comment)
 *
 * Syntax:
 *   dcnn_blunder [boosted] color coord                       expect blunder coord
 *   dcnn_blunder [boosted] color coord1 coord2               expect blunders coord1 and coord2
 *   dcnn_blunder [boosted] color coord1 coord2 !coord3       expect blunders coord1 and coord2, but not coord3 ... */
static bool
test_dcnn_blunder(board_t *b, char *arg)
{
	static int init = 0;
	if (!init) {
#ifndef FAKE_DCNN_OUTPUT
		dcnn_init(b);
#endif
		pattern_config_t pc;
		patterns_init(&pc, NULL, false, true);
		init = 1;
	}

	next_arg(arg);
	bool boosted = !strcmp(arg, "boosted");
	if (boosted)
		next_arg(arg);
	
	enum stone color = str2stone(arg);
	assert(color != S_NONE);
	next_arg(arg);

	char* args[30];  int n;
	mq_t wanted;    mq_init(&wanted);
	mq_t unwanted;  mq_init(&unwanted);
	for (n = 0; *arg; n++) {
		args[n] = arg;
		mq_t *q = (*arg == '!' ? &unwanted : &wanted);
		if (*arg == '!')  arg++;
		if (!strcmp(arg, "pass") || !strcmp(arg, "resign"))  die("Can't have pass or resign here.\n");
		if (!valid_coord(arg))  die("Invalid move: '%s'\n", arg);
		mq_add(q, str2coord(arg));
		next_arg_opt(arg);
	}
	if (!wanted.moves && !unwanted.moves)  die("No moves specified");
	args_end();
	
	board_print_test(b);
	if (boosted)  fprintf(stderr, "dcnn_blunder boosted %s ", stone2str(color));
	else          fprintf(stderr, "dcnn_blunder %s ", stone2str(color));
	for (int i = 0; i < n; i++)
		fprintf(stderr, "%s ", args[i]);
	fprintf(stderr, "...\n\n");

	/***********************************************************************/
	/* Get ownermap */
	ownermap_t ownermap;  ownermap_init(&ownermap);
	mcowner_playouts(b, color, &ownermap);

	/* Get dcnn output */
	float result[19 * 19];
#ifdef FAKE_DCNN_OUTPUT
	for (int i = 0; i < 19 * 19; i++)  /* Fake dcnn output */
		result[i] = 0.015;         /* All moves 1.5%   (less than 2% so we can test boosted move trimming) */
#else
	dcnn_evaluate_raw(b, color, result, &ownermap, DEBUGL(2));
#endif

	/* Get blunders */
	mq_t blunders;  mq_init(&blunders);
	get_dcnn_blunders(boosted, b, color, result, &ownermap, &blunders);
	//fprintf(stderr, "found %i\n", blunders.moves);

	/* Still run regular blunder code so they get logged */
	dcnn_fix_blunders(b, color, result, &ownermap, DEBUGL(2));
	
	/***********************************************************************/	
	/* Check results */
	bool eres = true, rres = true;
	for (int i = 0; i < wanted.moves; i++)
		if (!mq_has(&blunders, wanted.move[i]))
			rres = false;
	
	for (int i = 0; i < unwanted.moves; i++)
		if (mq_has(&blunders, unwanted.move[i]))
			rres = false;
	
	PRINT_RES();
	
	return rres;
}

static bool
test_first_line_blunder(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "first_line_blunder %s %s %d...\t", stone2str(color), coord2sstr(c), eres);
	
	assert(board_at(b, c) == S_NONE);	/* Sanity checks */
	assert(coord_edge_distance(c) == 0);
	with_move_strict(b, c, color, {
		group_t g = group_at(b, c);
		assert(g);
		assert(group_stone_count(b, g, 4) >= 3);
		assert(board_group_info(b, g).libs == 3 ||
		       board_group_info(b, g).libs == 2);
	});
	
	move_t m = move(c, color);
	int rres = dcnn_first_line_connect_blunder(b, &m);
	
	PRINT_RES();
	return   (rres == eres);
}

#endif /* DCNN */

bool board_undo_stress_test(board_t *orig, char *arg);
bool board_regression_test(board_t *orig, char *arg);
bool moggy_regression_test(board_t *orig, char *arg);
bool spatial_regression_test(board_t *orig, char *arg);

typedef bool (*t_unit_func)(board_t *board, char *arg);

typedef struct {
	char *cmd;
	t_unit_func f;
} t_unit_cmd;

static t_unit_cmd commands[] = {
	{ "bad_selfatari",          test_bad_selfatari,         },
	{ "really_bad_selfatari",   test_really_bad_selfatari,  },
	{ "3lib_selfatari",         test_3lib_selfatari,        },
	{ "snapback",		    test_snapback,		},
	{ "ladder",                 test_ladder,                },
	{ "ladder_any",             test_ladder_any,            },
	{ "wouldbe_ladder",         test_wouldbe_ladder,        },
	{ "wouldbe_ladder_any",     test_wouldbe_ladder_any,    },
	{ "useful_ladder",          test_useful_ladder,         },
	{ "can_countercap",         test_can_countercap,        },
	{ "atari",		    test_atari			},
	{ "two_eyes",               test_two_eyes,              },
	{ "moggy moves",            test_moggy_moves,           },
	{ "moggy status",           test_moggy_status,          },
	{ "moggy debug_game",       moggy_debug_game,           },
	{ "false_eye_seki",         test_false_eye_seki,        },
	{ "breaking_3_stone_seki",  test_breaking_3_stone_seki,},
	{ "pass_is_safe",           test_pass_is_safe,          },
	{ "final_score",            test_final_score,           },
	{ "genmove",		    test_genmove                },
#ifdef DCNN
	{ "dcnn_blunder",	    test_dcnn_blunder           },
	{ "first_line_blunder",     test_first_line_blunder     },
#endif
#ifdef BOARD_TESTS
	{ "board_undo_stress_test", board_undo_stress_test      },
	{ "board_regtest",          board_regression_test       },
	{ "moggy_regtest",          moggy_regression_test       },
	{ "spatial_regtest",        spatial_regression_test     },
#endif

/* Aliases */
	{ "sar",                    test_bad_selfatari,             },  /* backward compatibility */
	{ "rbsar",                  test_really_bad_selfatari,      },
	
	{ 0, 0 }
};

int
unit_test_cmd(board_t *b, char *line)
{
	board_printed = false;
	chomp(line);
	remove_comments(line);

	int optional = 0;  /* for t-unit over gtp ... */
	if (line[0] == '!') {  optional = 1;  line++;  }
	
	for (int i = 0; commands[i].cmd; i++) {
		char *cmd = commands[i].cmd;
		if (!str_prefix(cmd, line))
			continue;
		char c = line[strlen(cmd)];
		if (c && c != ' ' && c != '\t')
			continue;

		init_arg_len(line, strlen(cmd));
		bool r = commands[i].f(b, next);
		if (!r && !optional && pachi_options()->tunit_fatal)
			exit(EXIT_FAILURE);
		return r;
	}

	die("Syntax error: %s\n", line);
}


int
unit_test(char *filename)
{
	tunit_over_gtp = 0;
	
	FILE *f = fopen(filename, "r");
	if (!f)  fail(filename);
	
	int total = 0, passed = 0;
	int total_opt = 0, passed_opt = 0;
	
	board_t *b = board_new(19, NULL);
	b->komi = 7.5;
	char buf[256]; char *line = buf;

	while (fgets(line, sizeof(buf), f)) {
		chomp(line);
		remove_comments(line);
		
		optional = 0;
		switch (line[0]) {
			case '%':
				strncpy(title, line, sizeof(title)-1);
				title[sizeof(title)-1] = 0;
				if (DEBUGL(1))  fprintf(stderr, "\n%s\n", line);
				continue;
			case '!':
				optional = 1; line++;
				line += strspn(line, " ");
				break;
			case  0 : continue;
		}

		if (str_prefix("boardsize ", line)) {  init_arg(line); board_load(b, f, next); continue;  }
		if (str_prefix("rules ", line))     {  init_arg(line); set_rules(b, next); continue;  }
		if (str_prefix("komi ", line))      {  init_arg(line); set_komi(b, next); continue;  }
		if (str_prefix("ko ", line))	    {  init_arg(line); set_ko(b, next); continue;  }
		
		if (optional)  {  total_opt++;  passed_opt += unit_test_cmd(b, line); }
		else           {  total++;      passed     += unit_test_cmd(b, line); }
	}

	fclose(f);
	board_delete(&b);

	printf("\n\n");
	printf("----------- [  %3i/%-3i mandatory tests passed (%i%%)  ] -----------\n", passed, total, passed * 100 / total);
	if (total_opt)
	printf("               %3i/%-3i  optional tests passed (%i%%)               \n\n", passed_opt, total_opt, passed_opt * 100 / total_opt);

	int ret = 0;
 	if (total == passed)  printf("\nAll tests PASSED");
	else               {  printf("\nSome tests FAILED");  ret = EXIT_FAILURE;  }
	if (passed_opt != total_opt)
		printf(", %d optional test(s) IGNORED", total_opt - passed_opt);
	printf("\n");
	return ret;
}
