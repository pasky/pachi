#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "patternscan/patternscan.h"
#include "pattern.h"


/* Internal engine state. */
struct patternscan {
	int debug_level;
};


static char *
patternscan_play(struct engine *e, struct board *b, struct move *m)
{
	if (is_resign(m->coord))
		return NULL;

	/* Scan for supported features. */
	/* For specifiation of features and their payloads,
	 * please refer to pattern.h. */
	struct pattern p;
	/* TODO: Configurable pattern_config. */
	struct pattern_config pc = DEFAULT_PATTERN_CONFIG;
	pattern_get(&pc, &p, b, m);

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


struct patternscan *
patternscan_state_init(char *arg)
{
	struct patternscan *ps = calloc(1, sizeof(struct patternscan));

	ps->debug_level = 1;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					ps->debug_level = atoi(optval);
				else
					ps->debug_level++;
			} else {
				fprintf(stderr, "patternscan: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	return ps;
}

struct engine *
engine_patternscan_init(char *arg)
{
	struct patternscan *ps = patternscan_state_init(arg);
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "PatternScan Engine";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of games fed to it as GTP streams for various pattern features.";
	e->genmove = patternscan_genmove;
	e->notify_play = patternscan_play;
	e->data = ps;

	return e;
}
