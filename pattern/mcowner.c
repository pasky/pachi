#include <assert.h>
#include <stdio.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "ownermap.h"
#include "playout.h"
#include "playout/moggy.h"
#include "pattern/mcowner.h"


/******************************************************************************************/
/* MCowner playouts */

static void
mcowner_playouts_(board_t *b, enum stone color, ownermap_t *ownermap, int playouts)
{
	static playout_policy_t *policy = NULL;
	playout_setup_t setup = playout_setup(MAX_GAMELEN, 0);
	playout_t playout = { &setup, policy };
	
	if (!policy)  policy = playout_moggy_init(NULL, b);
	ownermap_init(ownermap);
	
	for (int i = 0; i < playouts; i++)  {
		board_t b2;
		board_copy(&b2, b);
		playout_play_game(&playout, &b2, color, NULL, ownermap);
		board_done(&b2);
	}
	//fprintf(stderr, "pattern ownermap:\n");
	//board_print_ownermap(b, stderr, ownermap);
}

void
mcowner_playouts(board_t *b, enum stone color, ownermap_t *ownermap)
{
	mcowner_playouts_(b, color, ownermap, GJ_MINGAMES);
}

void
mcowner_playouts_fast(board_t *b, enum stone color, ownermap_t *ownermap)
{
	mcowner_playouts_(b, color, ownermap, 100);
}
