#define DEBUG
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "board.h"
#include "debug.h"
#include "tactics/selfatari.h"
#include "tactics/dragon.h"
#include "tactics/ladder.h"
#include "tactics/1lib.h"
#include "tactics/2lib.h"
#include "tactics/seki.h"
#include "util.h"
#include "random.h"
#include "playout.h"
#include "timeinfo.h"
#include "playout/moggy.h"
#include "engines/replay.h"
#include "ownermap.h"


/* Running tests over gtp ? */
static bool tunit_over_gtp = 1;
static bool board_printed;
static bool last_move_set;
static char *next;

#define next_arg(to_) \
	to_ = next; \
	next += strcspn(next, " \t"); \
	if (*next) { \
		*next = 0; next++; \
		next += strspn(next, " \t"); \
	} \

static void
chomp(char *line)
{
	int n = strlen(line);
	if (line[n - 1] == '\n')
		line[n - 1] = 0;
}

static void
remove_comments(char *line)
{
	if (strchr(line, '#'))
		*strchr(line, '#') = 0;
}

/* Check no more args */
static void
args_end()
{
	if (*next)  die("Invalid extra arg: '%s'\n", next);
}

static void
board_print_test(int level, board_t *b)
{
	if (!DEBUGL(level) || board_printed)
		return;
	board_print(b, stderr);
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
	assert(*arg == '-' || *arg == '+' || isdigit(*arg));
	b->komi = atof(arg);
}

static void
set_handicap(board_t *b, char *arg)
{
	assert(isdigit(*arg));
	b->handicap = atoi(arg);
}

static void
board_load(board_t *b, FILE *f, int size)
{
	move_t last_move = move(pass, S_NONE);
	last_move_set = false;
	board_resize(b, size);
	board_clear(b);
	for (int y = size - 1; y >= 0; y--) {
		char line[256];
		if (!fgets(line, sizeof(line), f))  die("Premature EOF.\n");
		chomp(line);
		remove_comments(line);

		if (!strncmp(line, "komi ", 5))     {  set_komi(b, line + 5);     y++; continue;  }
		if (!strncmp(line, "handicap ", 9)) {  set_handicap(b, line + 9); y++; continue;  }

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
	assert(isalpha(*arg));
	move_t last;
	last.coord = str2coord(arg);
	last.color = board_at(b, last.coord);
	assert(last.color == S_BLACK || last.color == S_WHITE);
	b->last_move = last;

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
show_title_if_needed(int passed)
{
	if (!passed && debug_level == 1 && *title) {
		fprintf(stderr, "\n%s\n", title);
		*title = 0;
	}
}

#define PRINT_TEST(board, format, ...)	do {			   \
	board_print_test(2, board);				   \
	if (DEBUGL(1))  fprintf(stderr, format, __VA_ARGS__);	   \
} while(0)

#define PRINT_RES(passed)  do {						\
	if (!(passed)) {					\
		show_title_if_needed(passed);			\
		if (DEBUGL(0))  fprintf(stderr, "FAILED %s\n", (optional ? "(optional)" : "")); \
	} else								\
		if (DEBUGL(1))  fprintf(stderr, "OK\n");		\
} while(0)


static bool
test_sar(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "sar %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = is_bad_selfatari(b, color, c);

	PRINT_RES(rres == eres);
	return   (rres == eres);
}

static bool
test_corner_seki(board_t *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2coord(arg);
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

	PRINT_TEST(b, "corner_seki %s %s %d...\t", stone2str(color), coord2sstr(c), eres);

	assert(board_at(b, c) == S_NONE);
	int rres = breaking_corner_seki(b, c, color);

	PRINT_RES(rres == eres);
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

	PRINT_RES(rres == eres);
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
	
	PRINT_RES(rres == eres);
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
	
	PRINT_RES(rres == eres);
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
	
	PRINT_RES(rres == eres);
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
	
	PRINT_RES(rres == eres);
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
	
	PRINT_RES(rres == eres);
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
	int rres = can_countercapture(b, g, NULL, 0);

	PRINT_RES(rres == eres);
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

	PRINT_RES(rres == eres);
	return   (rres == eres);
}


/* Sample moves played by moggy in a given position.
 * Board last move matters quite a lot and must be set.
 * 
 * Syntax:  moggy moves
 */
static bool
test_moggy_moves(board_t *b, char *arg)
{
	int runs = 1000;

	args_end();
	if (!tunit_over_gtp) assert(last_move_set);
	board_print_test(2, b);

	char e_arg[128];  sprintf(e_arg, "runs=%i", runs);
	engine_t e;  engine_init(&e, E_REPLAY, e_arg, b);
	enum stone color = stone_other(b->last_move.color);
	
	if (DEBUGL(2))
		fprintf(stderr, "moggy moves, %s to play. Sampling moves (%i runs)...\n\n",
			stone2str(color), runs);

        int played_[b->size2 + 1];		memset(played_, 0, sizeof(played_));
	int *played = played_ + 1;		// allow storing pass
	int most_played = 0;
	replay_sample_moves(&e, b, color, played, &most_played);

	/* Show moves stats */	
	for (int k = most_played; k > 0; k--)
		for (coord_t c = pass; c < b->size2; c++)
			if (played[c] == k)
				if (DEBUGL(2)) fprintf(stderr, "%3s: %.2f%%\n", coord2str(c), (float)k * 100 / runs);
	
	engine_done(&e);
	return true;   // Not much of a unit test right now =)
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
		board_done_noalloc(&b2);
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
	next_arg(arg);

	bool speed_benchmark = !*arg;
	int games = (speed_benchmark ? 4000 : 500);
	coord_t              status_at[10];
	enum point_judgement expected[10];
	int                  thres[10];
	int n = 0;
	
	for (n = 0; *arg; n++) {
		if (!isalpha(*arg))  die("Invalid arg: '%s'\n", arg);
		status_at[n] = str2coord(arg);
		next_arg(arg);

		if (!*arg || strlen(arg) != 1) die("Expected x/o/X/O/: after coord %s\n", coord2sstr(status_at[n]));
		thres[n] = 67;
		if (!strcmp(arg, "X") || !strcmp(arg, "O" )) thres[n] = 80;
		if      (!strcasecmp(arg, "x"))  expected[n] = PJ_BLACK;
		else if (!strcasecmp(arg, "o"))  expected[n] = PJ_WHITE;
		else if (!strcasecmp(arg, ":"))  expected[n] = PJ_SEKI;
		else if (!strcasecmp(arg, "?"))  { expected[n] = PJ_BLACK; thres[n] = 0;  }
		else    die("Expected x/o/X/O/: after coord %s\n", coord2sstr(status_at[n]));
		next_arg(arg);
	}
	args_end();
	
	if (!tunit_over_gtp) assert(last_move_set);
	
	enum stone color = (is_pass(b->last_move.coord) ? S_BLACK : stone_other(b->last_move.color));
	board_print_test(2, b);
	if (DEBUGL(2))  fprintf(stderr, "moggy status ");
	for (int i = 0; i < n; i++) {
		const char *chr = (thres[i] == 80 ? ":XO," : ":xo,");
		if (!thres[i])  chr = "????";
		if (DEBUGL(2)) fprintf(stderr, "%s %c  ", coord2sstr(status_at[i]),	chr[expected[i]]);
	}
	if (DEBUGL(2)) fprintf(stderr, "\n%s to play. Playing %i games ...\n", stone2str(color), games);

	/* Get final status estimate after a number of moggy games */	
	ownermap_t ownermap;
	int wr = moggy_games(b, color, games, &ownermap, speed_benchmark);

	int wr_black = wr * 100 / games;
	int wr_white = (games - wr) * 100 / games;
	if (wr_black > wr_white)  { if (DEBUGL(2)) fprintf(stderr, "Winrate: [ black %i%% ]  white %i%%\n\n", wr_black, wr_white); }
	else		            if (DEBUGL(2)) fprintf(stderr, "Winrate: black %i%%  [ white %i%% ]\n\n", wr_black, wr_white);

	if (DEBUGL(2)) board_print_ownermap(b, stderr, &ownermap);

	/* Check results */
	bool ret = true;
	for (int i = 0; i < n; i++) {
		coord_t c = status_at[i];
		enum point_judgement j = ownermap_judge_point(&ownermap, c, 0.8);
		if (j == PJ_UNKNOWN) j = ownermap_judge_point(&ownermap, c, 0.67);		
		enum stone color = (enum stone)j;
		if (j == PJ_UNKNOWN)
			color = (ownermap.map[c][S_BLACK] > ownermap.map[c][S_WHITE] ? S_BLACK : S_WHITE);
		int pc = ownermap.map[c][color] * 100 / ownermap.playouts;

		int passed = (!thres[i] || (j == expected[i] && pc >= thres[i]));
		const char *colorstr = (j == PJ_SEKI ? "seki" : stone2str(color));
		PRINT_TEST(b, "moggy status %3s %-5s -> %3i%%    ", coord2sstr(c), colorstr, pc);
		
		if (!passed)  ret = false;
		PRINT_RES(passed);
	}
	
	return ret;
}

bool board_undo_stress_test(board_t *orig, char *arg);
bool board_regression_test(board_t *orig, char *arg);
bool moggy_regression_test(board_t *orig, char *arg);
bool spatial_regression_test(board_t *orig, char *arg);

typedef bool (*t_unit_func)(board_t *board, char *arg);

typedef struct {
	char *cmd;
	t_unit_func f;
	bool needs_arg;
} t_unit_cmd;

static t_unit_cmd commands[] = {
	{ "sar",                    test_sar,               1 },
	{ "ladder",                 test_ladder,            1 },
	{ "ladder_any",             test_ladder_any,        1 },
	{ "wouldbe_ladder",         test_wouldbe_ladder,    1 },
	{ "wouldbe_ladder_any",     test_wouldbe_ladder_any,1 },
	{ "useful_ladder",          test_useful_ladder,     1 },
	{ "can_countercap",         test_can_countercap,    1 },
	{ "two_eyes",               test_two_eyes,          1 },
	{ "moggy moves",            test_moggy_moves,       0 },
	{ "moggy status",           test_moggy_status,      0 },
	{ "corner_seki",            test_corner_seki,       1 },
	{ "false_eye_seki",         test_false_eye_seki,    1 },
#ifdef BOARD_TESTS
	{ "board_undo_stress_test", board_undo_stress_test, 0 },
	{ "board_regtest",          board_regression_test,  0 },
	{ "moggy_regtest",          moggy_regression_test,  0 },
	{ "spatial_regtest",        spatial_regression_test,  0 },
#endif
	{ 0, 0, 0 }
};

int
unit_test_cmd(board_t *b, char *line)
{
	board_printed = false;
	chomp(line);
	remove_comments(line);
	
	for (int i = 0; commands[i].cmd; i++) {
		char *cmd = commands[i].cmd;
		if (!str_prefix(cmd, line))
			continue;
		char c = line[strlen(cmd)];
		if (c && c != ' ' && c != '\t')
			continue;
		if (commands[i].needs_arg && c != ' ')
			die("%s\nerror: command %s needs argument(s)\n", line, cmd);
		
		next = line + strlen(cmd);
		next += strspn(next, " \t");
		return commands[i].f(b, next);
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
	
	board_t *b = board_new(19+2, NULL);
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
		
		if (!strncmp(line, "boardsize ", 10))  {  board_load(b, f, atoi(line + 10)); continue;  }
		if (!strncmp(line, "ko ", 3))	       {  set_ko(b, line + 3); continue;  }

		if (optional)  {  total_opt++;  passed_opt += unit_test_cmd(b, line); }
		else           {  total++;      passed     += unit_test_cmd(b, line); }
	}

	fclose(f);

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
