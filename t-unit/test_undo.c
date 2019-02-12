#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "playout.h"
#include "playout/light.h"


static void
board_dump_group(board_t *b, group_t g)
{
        printf("group base: %s  color: %s  libs: %i  stones: %i\n",
               coord2sstr(g), stone2str(board_at(b, g)),
               board_group_info(b, g).libs, group_stone_count(b, g, 500));

        printf("  stones: ");
        foreach_in_group(b, g) {
                printf("%s ", coord2sstr(c));
        } foreach_in_group_end;
        printf("\n");

        printf("  libs  : ");   
        for (int i = 0; i < board_group_info(b, g).libs; i++) {
                coord_t lib = board_group_info(b, g).lib[i];
                printf("%s ", coord2sstr(lib));
        }
        printf("\n");
}

static void
board_dump(board_t *b)
{       
        printf("board_dump(): size: %i  size2: %i  bits2: %i\n", 
               b->size, b->size2, b->bits2);
        board_print(b, stdout);

        printf("ko: %s %s  last_ko: %s %s  last_ko_age: %i\n",
               stone2str(b->ko.color), coord2sstr(b->ko.coord),
               stone2str(b->last_ko.color), coord2sstr(b->last_ko.coord),
               b->last_ko_age);

        printf("groups: \n");
        int seen[BOARD_MAX_COORDS] = {0, };
        foreach_point(b) {
                if (board_at(b, c) != S_BLACK && board_at(b, c) != S_WHITE)
                        continue;
                group_t g = group_at(b, c);
                if (seen[g])
                        continue;
                seen[g] = 1;
                board_dump_group(b, g);
        } foreach_point_end;

        printf("\n");   
}


// Print info about suicides
static void
show_suicide_info(board_t *b, board_t *orig, coord_t c, enum stone color)
{
	if (board_at(b, c) != S_NONE)
		return;
	
	int groups[4] = { 0, };
	int n = 0, stones = 0;
	foreach_neighbor(orig, c, {
			if (board_at(orig, c) != color)
				continue;
			group_t g = group_at(orig, c);
			int i;
			for (i = 0; groups[i] && groups[i] != g; i++)
				;
			groups[i] = g;
			if (i > n)
				n = i;
			stones += group_stone_count(orig, g, 400);
		});

	if (++n > 1)
		fprintf(stderr, "multi-group suicide: %i groups    %i stones\n", n, stones);
}


/* Play move and check board states after quick_play() / quick_undo() match */
static coord_t
test_undo(board_t *orig, coord_t c, enum stone color)
{
	board_t b, b2;
	board_copy(&b, orig);
	board_copy(&b2, orig);

	move_t m = move(c, color);
	int r = board_play(&b, &m);  assert(r >= 0);

	with_move(&b2, c, color, {
		// Check state after quick_board_play() matches
		assert(!board_quick_cmp(&b2, &b));  
	});

	if (DEBUGL(3))
		show_suicide_info(&b, orig, c, color);
	if (DEBUGL(4))
		board_print(&b, stderr);

	// Check board_quick_undo() restored board properly
	if (board_quick_cmp(&b2, orig) || board_cmp(&b2, orig)) {
		board_dump(orig);
		board_dump(&b2);
		assert(0);
	}

	board_done_noalloc(&b);
	board_done_noalloc(&b2);
	
	return c;
}


static playoutp_permit policy_permit = NULL;

static bool
permit_hook(playout_policy_t *playout_policy, board_t *b, move_t *m, bool alt, bool rnd)
{
	test_undo(b, m->coord, m->color);

	/* Also test pass, permit() never gets called on pass ... */
	if (fast_random(100) < 5)
		test_undo(b, pass, m->color);
	
	return (policy_permit ? policy_permit(playout_policy, b, m, alt, rnd) : true);
}


/* Play some random games testing undo on every move. */
bool
board_undo_stress_test(board_t *board, char *arg)
{
	int games = 100;
	enum stone color = S_BLACK;
	
	if (DEBUGL(2))  board_print(board, stderr);
	if (DEBUGL(1))  printf("board_undo stress test.   Playing %i games checking every move + pass...\n", games);

	// Light policy better to test wild multi-group suicides
	playout_policy_t *policy = playout_light_init(NULL, board);
	playout_setup_t setup = playout_setup(MAX_GAMELEN, 0);
	
	// Hijack policy permit()
	policy_permit = policy->permit;  policy->permit = permit_hook;

	/* Play some games */
	for (int i = 0; i < games; i++)  {
		board_t b;
		board_copy(&b, board);		
		playout_play_game(&setup, &b, color, NULL, NULL, policy);
		board_done_noalloc(&b);
	}
	
	printf("All good.\n\n");
	return true;
}
