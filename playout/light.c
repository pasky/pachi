#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "playout.h"
#include "playout/light.h"
#include "random.h"


#define PLDEBUGL(n) DEBUGL_(p->debug_level, n)


coord_t
playout_light_choose(playout_policy_t *p, playout_setup_t *s, board_t *b, enum stone to_play)
{
	return pass;
}


playout_policy_t *
playout_light_init(char *arg, board_t *b)
{
	playout_policy_t *p = calloc2(1, playout_policy_t);
	p->choose = playout_light_choose;

	if (arg)
		fprintf(stderr, "playout-light: This policy does not accept arguments (%s)\n", arg);

	return p;
}
