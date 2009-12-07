#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "patternscan/patternscan.h"
#include "pattern.h"


static char *
patternscan_play(struct engine *e, struct board *b, struct move *m)
{
	if (is_resign(m->coord))
		return NULL;

	/* Scan for supported features. */
	/* For specifiation of features and their payloads,
	 * please refer to pattern.h. */
	struct pattern p;
	pattern_get(&p, b, m);

	static char str[8192]; // XXX
	*str = 0;
	pattern2str(str, &p);
	return str;
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
