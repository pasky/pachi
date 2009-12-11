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
	struct pattern_config pc;
};


static char *
patternscan_play(struct engine *e, struct board *b, struct move *m)
{
	struct patternscan *ps = e->data;

	if (is_resign(m->coord))
		return NULL;

	/* Scan for supported features. */
	/* For specifiation of features and their payloads,
	 * please refer to pattern.h. */
	struct pattern p;
	pattern_match(&ps->pc, &p, b, m);

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
	ps->pc = DEFAULT_PATTERN_CONFIG;

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
			/* See pattern.h:pattern_config for description and
			 * pattern.c:DEFAULT_PATTERN_CONFIG for default values. */
			} else if (!strcasecmp(optname, "spat_min") && optval) {
				ps->pc.spat_min = atoi(optval);
			} else if (!strcasecmp(optname, "spat_max") && optval) {
				ps->pc.spat_max = atoi(optval);
			} else if (!strcasecmp(optname, "bdist_max") && optval) {
				ps->pc.bdist_max = atoi(optval);
			} else if (!strcasecmp(optname, "ldist_min") && optval) {
				ps->pc.ldist_min = atoi(optval);
			} else if (!strcasecmp(optname, "ldist_max") && optval) {
				ps->pc.ldist_max = atoi(optval);
			} else if (!strcasecmp(optname, "mcsims") && optval) {
				ps->pc.mcsims = atoi(optval);
			} else {
				fprintf(stderr, "patternscan: Invalid engine argument %s or missing value\n", optname);
				exit(EXIT_FAILURE);
			}
		}
	}
	ps->pc.spat_dict = spatial_dict_init(true);

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
