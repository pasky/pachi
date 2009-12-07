#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "patternscan/patternscan.h"


static void
patternscan_play(struct engine *e, struct board *b, struct move *m)
{
	if (is_resign(m->coord))
		return;

	/* Scan for various features now. */
	/* TODO: So far, no features are supported. */
	return;
}

static coord_t *
patternscan_genmove(struct engine *e, struct board *b, enum stone color)
{
	fprintf(stderr, "genmove command not available during patternscan!\n");
	exit(EXIT_FAILURE);
}

struct engine *
engine_patternscan_init(char *arg)
{
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "PatternScan Engine";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of games fed to it as GTP streams for various pattern features.";
	e->genmove = patternscan_genmove;
	e->notify_play = patternscan_play;

	if (arg)
		fprintf(stderr, "PatternScan: I support no engine arguments\n");

	return e;
}
