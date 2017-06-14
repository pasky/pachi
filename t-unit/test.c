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
#include "random.h"
#include "playout.h"
#include "timeinfo.h"
#include "playout/moggy.h"
#include "engines/replay.h"
#include "ownermap.h"

/* Running tests over gtp ? */
static bool tunit_over_gtp = 1;
static bool board_printed;
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
	if (*next) {
		fprintf(stderr, "Invalid extra arg: '%s'\n", next);
		exit(EXIT_FAILURE);
	}
}

static void
board_print_test(int level, struct board *b)
{
	if (!DEBUGL(level) || board_printed)
		return;
	board_print(b, stderr);
	board_printed = true;
}

static void
board_load(struct board *b, FILE *f, unsigned int size)
{
	board_resize(b, size);
	board_clear(b);
	for (int y = size - 1; y >= 0; y--) {
		char line[256];
		if (!fgets(line, sizeof(line), f)) {
			fprintf(stderr, "Premature EOF.\n");
			exit(EXIT_FAILURE);
		}

		chomp(line);
		remove_comments(line);		
		if (strlen(line) != size * 2 - 1) {
			fprintf(stderr, "Line not %d char long: '%s'\n", size * 2 - 1, line);
			exit(EXIT_FAILURE);
		}
		
		for (unsigned int i = 0; i < size * 2; i++) {
			enum stone s;
			switch (line[i]) {
				case '.': s = S_NONE; break;
				case 'X': s = S_BLACK; break;
				case 'O': s = S_WHITE; break;
				default: fprintf(stderr, "Invalid stone '%c'\n", line[i]);
					 exit(EXIT_FAILURE);
			}
			i++;
			if (line[i] != ' ' && i/2 < size - 1) {
				fprintf(stderr, "No space after stone %i: '%c'\n", i/2 + 1, line[i]);
				exit(EXIT_FAILURE);
			}
			if (s == S_NONE) continue;
			struct move m = { .color = s, .coord = coord_xy(b, i/2 + 1, y + 1) };
			if (board_play(b, &m) < 0) {
				fprintf(stderr, "Failed to play %s %s\n",
					stone2str(s), coord2sstr(m.coord, b));
				board_print(b, stderr);
				exit(EXIT_FAILURE);
			}
		}
	}
	int suicides = b->captures[S_BLACK] || b->captures[S_WHITE];
	assert(!suicides);
}

static void
set_ko(struct board *b, char *arg)
{
	assert(isalpha(*arg));
	struct move last;
	last.coord = str2scoord(arg, board_size(b));
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


#define PRINT_TEST(board)    \
	board_print_test(2, board); \
	if (DEBUGL(1))  \
		fprintf(stderr, TEST_PRINTF)

#define CHECK_TEST(rres, eres, board)			\
	if (rres != eres) { \
		if (debug_level <= 2) { \
			board_print_test(0, board); \
                        if (debug_level != 2) \
				fprintf(stderr, TEST_PRINTF);	\
		} \
		fprintf(stderr, "FAILED (%d)\n", rres);	\
	} \
	else \
		if (DEBUGL(1))  fprintf(stderr, "OK\n");


static bool
test_sar(struct board *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2scoord(arg, board_size(b));
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

#define TEST_PRINTF "sar %s %s %d...\t", stone2str(color), coord2sstr(c, b), eres
	PRINT_TEST(b);

	assert(board_at(b, c) == S_NONE);
	int rres = is_bad_selfatari(b, color, c);

	CHECK_TEST(rres, eres, b);
	return (rres == eres);
#undef  TEST_PRINTF
}


static bool
test_ladder(struct board *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2scoord(arg, board_size(b));
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

#define TEST_PRINTF "ladder %s %s %d...\t", stone2str(color), coord2sstr(c, b), eres
	PRINT_TEST(b);
	
	assert(board_at(b, c) == S_NONE);
	group_t atari_neighbor = board_get_atari_neighbor(b, c, color);
	assert(atari_neighbor);
	int rres = is_ladder(b, c, atari_neighbor, true);
	
	CHECK_TEST(rres, eres, b);
	return (rres == eres);
#undef  TEST_PRINTF
}


static bool
test_useful_ladder(struct board *b, char *arg)
{
	next_arg(arg);
	enum stone color = str2stone(arg);
	next_arg(arg);
	coord_t c = str2scoord(arg, board_size(b));
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

#define TEST_PRINTF "useful_ladder %s %s %d...\t", stone2str(color), coord2sstr(c, b), eres
	PRINT_TEST(b);
	
	assert(board_at(b, c) == S_NONE);
	group_t atari_neighbor = board_get_atari_neighbor(b, c, color);
	assert(atari_neighbor);
	int ladder = is_ladder(b, c, atari_neighbor, true);  assert(ladder);
	int rres = useful_ladder(b, atari_neighbor);
	
	CHECK_TEST(rres, eres, b);
	return (rres == eres);
#undef  TEST_PRINTF
}

static bool
test_can_countercap(struct board *b, char *arg)
{
	next_arg(arg);
	coord_t c = str2scoord(arg, board_size(b));
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

#define TEST_PRINTF "can_countercap %s %d...\t", coord2sstr(c, b), eres
	PRINT_TEST(b);

	enum stone color = board_at(b, c);
	group_t g = group_at(b, c);
	assert(color == S_BLACK || color == S_WHITE);
	int rres = can_countercapture(b, g, NULL, 0);

	CHECK_TEST(rres, eres, b);
	return (rres == eres);
#undef  TEST_PRINTF
}


static bool
test_two_eyes(struct board *b, char *arg)
{
	next_arg(arg);
	coord_t c = str2scoord(arg, board_size(b));
	next_arg(arg);
	int eres = atoi(arg);
	args_end();

#define TEST_PRINTF "two_eyes %s %d...\t", coord2sstr(c, b), eres
	PRINT_TEST(b);

	enum stone color = board_at(b, c);
	assert(color == S_BLACK || color == S_WHITE);
	int rres = dragon_is_safe(b, group_at(b, c), color);

	CHECK_TEST(rres, eres, b);
	return (rres == eres);
#undef  TEST_PRINTF
}


/* Sample moves played by moggy in a given position.
 * 
 * Syntax (file):  moggy moves (last_move)
 *        (gtp) :  moggy moves
 */
static bool
test_moggy_moves(struct board *b, char *arg)
{
	int runs = 1000;

	if (!tunit_over_gtp) {
		next_arg(arg);
		assert(*arg++ == '(');
		struct move last;
		last.coord = str2scoord(arg, board_size(b));
		last.color = board_at(b, last.coord);
		assert(last.color == S_BLACK || last.color == S_WHITE);
		next_arg(arg);		
		b->last_move = last;
	}
	args_end();

	board_print(b, stderr);  // Always print board so we see last move

	char e_arg[128];  sprintf(e_arg, "runs=%i", runs);
	struct engine *e = engine_replay_init(e_arg, b);
	enum stone color = stone_other(b->last_move.color);
	
	if (DEBUGL(1))
		fprintf(stderr, "moggy moves, %s to play. Sampling moves (%i runs)...\n\n",
			stone2str(color), runs);

        int played_[b->size2 + 2];		memset(played_, 0, sizeof(played_));
	int *played = played_ + 2;		// allow storing pass/resign
	int most_played = 0;
	replay_sample_moves(e, b, color, played, &most_played);

	/* Show moves stats */	
	for (int k = most_played; k > 0; k--)
		for (coord_t c = resign; c < b->size2; c++)
			if (played[c] == k)
				fprintf(stderr, "%3s: %.2f%%\n", coord2str(c, b), (float)k * 100 / runs);
	
	engine_done(e);
	return true;   // Not much of a unit test right now =)
}

#define board_empty(b) ((b)->flen == real_board_size(b) * real_board_size(b))

static void
pick_random_last_move(struct board *b, enum stone to_play)
{
	if (board_empty(b))
		return;
	
	int base = fast_random(board_size2(b));
	for (int i = base; i < base + board_size2(b); i++) {
		coord_t c = i % board_size2(b);
		if (board_at(b, c) == stone_other(to_play)) {
			b->last_move.coord = c;
			b->last_move.color = board_at(b, c);
			break;
		}
	}	
}


/* Syntax (file):
 *   moggy status (last_move) coord [coord...]
 *         Play number of random games starting from last_move
 * 
 *   moggy status     coord [coord...]
 *   moggy status (b) coord [coord...]
 *         Black to play, pick random white last move
 *
 *   moggy status (w) coord [coord...]  
 *         White to play, pick random black last move
 *
 * Syntax (gtp):
 *   Same but specifying last move is invalid here since last move is already known.
 *   Likewise no random last move is picked.
 */
static bool
test_moggy_status(struct board *board, char *arg)
{
	int games = 4000;
	coord_t status_at[10];
	int n = 0;
	enum stone color = (tunit_over_gtp ? stone_other(board->last_move.color) : S_BLACK);
	int pick_random = (!tunit_over_gtp);  // Pick random last move for each game

	next_arg(arg);
	while(*arg) {
		if (*arg == '(' && tunit_over_gtp) {
			fprintf(stderr, "Error: may not specify last move in gtp mode\n");
			exit(EXIT_FAILURE);
		}

		if (!strncmp(arg, "(b)", 3))
			color = S_BLACK;
		else if (!strncmp(arg, "(w)", 3))
			color = S_WHITE;
		else if (*arg == '(') {  /* Optional "(last_move)" argument */
			arg++;	assert(isalpha(*arg));
			pick_random = false;
			struct move last;
			last.coord = str2scoord(arg, board_size(board));
			last.color = board_at(board, last.coord);
			assert(last.color == S_BLACK || last.color == S_WHITE);
			color = stone_other(last.color);
			board->last_move = last;
		}
		else {
			assert(isalpha(*arg));
			status_at[n++] = str2scoord(arg, board_size(board));
		}
		next_arg(arg);
	}
	
	board_print(board, stderr);
	if (DEBUGL(1)) {
		fprintf(stderr, "moggy status ");
		for (int i = 0; i < n; i++)
			fprintf(stderr, "%s%s", coord2sstr(status_at[i], board), (i != n-1 ? " " : ""));
		fprintf(stderr, ", %s to play. Playing %i games %s...\n", 
		       stone2str(color), games, (pick_random ? "(random last move) " : ""));
	}
	
	struct playout_policy *policy = playout_moggy_init(NULL, board, NULL);
	struct playout_setup setup = { .gamelen = MAX_GAMELEN };
	struct board_ownermap ownermap;

	ownermap.playouts = 0;
	ownermap.map = malloc2(board_size2(board) * sizeof(ownermap.map[0]));
	memset(ownermap.map, 0, board_size2(board) * sizeof(ownermap.map[0]));	


	/* Get final status estimate after a number of moggy games */
	int wr = 0;
	double time_start = time_now();
	for (int i = 0; i < games; i++)  {
		struct board b;
		board_copy(&b, board);
		if (pick_random)
			pick_random_last_move(&b, color);
		
		int score = play_random_game(&setup, &b, color, NULL, &ownermap, policy);
		if (color == S_WHITE)
			score = -score;
		wr += (score > 0);
		board_done_noalloc(&b);
	}
	double elapsed = time_now() - time_start;
	fprintf(stderr, "moggy status in %.1fs, %i games/s\n\n", elapsed, (int)((float)games / elapsed));
	
	int wr_black = wr * 100 / games;
	int wr_white = (games - wr) * 100 / games;
	if (wr_black > wr_white)
		fprintf(stderr, "Winrate: [ black %i%% ]  white %i%%\n\n", wr_black, wr_white);
	else
		fprintf(stderr, "Winrate: black %i%%  [ white %i%% ]\n\n", wr_black, wr_white);

	board_print_ownermap(board, stderr, &ownermap);

	for (int i = 0; i < n; i++) {
		coord_t c = status_at[i];
		enum stone color = (ownermap.map[c][S_BLACK] > ownermap.map[c][S_WHITE] ? S_BLACK : S_WHITE);
		fprintf(stderr, "%3s owned by %s: %i%%\n", 
			coord2sstr(c, board), stone2str(color), 
			ownermap.map[c][color] * 100 / ownermap.playouts);
	}
	
	free(ownermap.map);
	playout_policy_done(policy);
	return true;   // Not much of a unit test right now =)
}

bool board_undo_stress_test(struct board *orig, char *arg);

typedef bool (*t_unit_func)(struct board *board, char *arg);

typedef struct {
	char *cmd;
	t_unit_func f;
	bool needs_arg;
} t_unit_cmd;

static t_unit_cmd commands[] = {
	{ "sar",                    test_sar,               1 },
	{ "ladder",                 test_ladder,            1 },
	{ "useful_ladder",          test_useful_ladder,     1 },
	{ "can_countercap",         test_can_countercap,    1 },
	{ "two_eyes",               test_two_eyes,          1 },
	{ "moggy moves",            test_moggy_moves,       0 },
	{ "moggy status",           test_moggy_status,      1 },
	{ "board_undo_stress_test", board_undo_stress_test, 0 },
	{ 0, 0, 0 }
};

int
unit_test_cmd(struct board *b, char *line)
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
		if (commands[i].needs_arg && c != ' ') {
			fprintf(stderr, "%s\n", line);
			fprintf(stderr, "error: command %s needs argument(s)\n", cmd);
			exit(EXIT_FAILURE);
		}
		
		next = line + strlen(cmd);
		next += strspn(next, " \t");
		return commands[i].f(b, next);
	}

	fprintf(stderr, "Syntax error: %s\n", line);
	exit(EXIT_FAILURE);
}


void
unit_test(char *filename)
{
	tunit_over_gtp = 0;
	
	FILE *f = fopen(filename, "r");
	if (!f) {
		perror(filename);
		exit(EXIT_FAILURE);
	}

	int total = 0;
	int passed = 0;
	int skipped = 0;
	
	struct board *b = board_init(NULL);
	b->komi = 7.5;
	char line[256];

	while (fgets(line, sizeof(line), f)) {
		chomp(line);
		remove_comments(line);
		
		switch (line[0]) {
			case '%': fprintf(stderr, "\n%s\n", line); continue;
			case '!': fprintf(stderr, "%s...\tSKIPPED\n", line); skipped++; continue;
			case  0 : continue;
		}
		
		if      (!strncmp(line, "boardsize ", 10))
			board_load(b, f, atoi(line + 10));
		else if (!strncmp(line, "ko ", 3))
			set_ko(b, line + 3);
		else {		
			total++;
			passed += unit_test_cmd(b, line);
		}
	}

	fclose(f);
	
	printf("\n\n----------- [  %i/%i tests passed (%i%%)  ] -----------\n\n", passed, total, passed * 100 / total);
 	if (total == passed)
		printf("\nAll tests PASSED");
	else {
		printf("\nSome tests FAILED\n");
		exit(EXIT_FAILURE);
	}
	if (skipped > 0)
		printf(", %d test(s) SKIPPED", skipped);
	printf("\n");
}
