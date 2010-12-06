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
playout_light_choose(struct playout_policy *p, struct playout_setup *s, struct board *b, enum stone to_play)
{
	return pass;
}


struct playout_policy *
playout_light_init(char *arg, struct board *b)
{
	struct playout_policy *p = calloc2(1, sizeof(*p));
	p->choose = playout_light_choose;

	if (arg)
		fprintf(stderr, "playout-light: This policy does not accept arguments (%s)\n", arg);

	return p;
}
