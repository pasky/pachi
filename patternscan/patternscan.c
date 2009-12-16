#include <assert.h>
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
	pattern_spec ps;
	bool competition;

	bool no_pattern_match;
	bool gen_spat_dict;
	/* Number of loaded spatials; checkpoint for saving new sids
	 * in case gen_spat_dict is enabled. */
	int loaded_spatials;
};


static void
process_pattern(struct patternscan *ps, struct board *b, struct move *m, char **str)
{
	/* First, store the spatial configuration in dictionary
	 * if applicable. */
	if (ps->gen_spat_dict && !is_pass(m->coord)) {
		struct spatial s;
		spatial_from_board(&ps->pc, &s, b, m);
		int dmax = s.dist;
		for (int d = 2; d <= dmax; d++) {
			s.dist = d;
			spatial_dict_put(ps->pc.spat_dict, &s, spatial_hash(0, &s) & spatial_hash_mask);
		}
	}

	/* Now, match the pattern. */
	if (!ps->no_pattern_match) {
		struct pattern p;
		pattern_match(&ps->pc, ps->ps, &p, b, m);
		*str = pattern2str(*str, &p);
	}
}

static char *
patternscan_play(struct engine *e, struct board *b, struct move *m)
{
	struct patternscan *ps = e->data;

	if (is_resign(m->coord))
		return NULL;

	static char str[1048576]; // XXX
	char *strp = str;
	*str = 0;

	/* Scan for supported features. */
	/* For specifiation of features and their payloads,
	 * please refer to pattern.h. */
	process_pattern(ps, b, m, &strp);

	if (ps->competition) {
		/* Look at other possible moves as well. */
		for (int f = 0; f < b->flen; f++) {
			struct move mo = { .coord = b->f[f], .color = m->color };
			if (mo.coord == m->coord)
				continue;
			if (!board_is_valid_move(b, &mo))
				continue;
			*strp++ = ' ';
			process_pattern(ps, b, &mo, &strp);
		}
	}

	return ps->no_pattern_match ? NULL : str;
}

static coord_t *
patternscan_genmove(struct engine *e, struct board *b, enum stone color)
{
	fprintf(stderr, "genmove command not available during patternscan!\n");
	exit(EXIT_FAILURE);
}

void
patternscan_finish(struct engine *e)
{
	struct patternscan *ps = e->data;
	if (!ps->gen_spat_dict)
		return;

	/* Save newly found patterns. */

	bool newfile = true;
	FILE *f = fopen(spatial_dict_filename, "r");
	if (f) { fclose(f); newfile = false; }
	f = fopen(spatial_dict_filename, "a");
	if (newfile)
		spatial_dict_writeinfo(ps->pc.spat_dict, f);

	for (int i = ps->loaded_spatials; i < ps->pc.spat_dict->nspatials; i++) {
		spatial_write(&ps->pc.spat_dict->spatials[i], i, f);
	}
	fclose(f);
}


struct patternscan *
patternscan_state_init(char *arg)
{
	struct patternscan *ps = calloc(1, sizeof(struct patternscan));

	ps->debug_level = 1;
	ps->pc = DEFAULT_PATTERN_CONFIG;
	memcpy(&ps->ps, PATTERN_SPEC_MATCHALL, sizeof(pattern_spec));

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

			} else if (!strcasecmp(optname, "gen_spat_dict")) {
				/* If set, re-generate the spatial patterns
				 * dictionary; you need to have a dictionary
				 * of spatial stone configurations in order
				 * to match any spatial features. */
				ps->gen_spat_dict = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "no_pattern_match")) {
				/* If set, do not actually match patterns.
				 * Useful only together with gen_spat_dict
				 * when just building spatial dictionary. */
				ps->no_pattern_match = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "competition")) {
				/* In competition mode, first the played
				 * pattern is printed, then all other patterns
				 * that could be played but weren't. */
				ps->competition = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "matchfast")) {
				/* Limit the matched features only to the
				 * set used in MC simulations. */
				ps->pc = FAST_PATTERN_CONFIG;
				memcpy(&ps->ps, PATTERN_SPEC_MATCHFAST, sizeof(pattern_spec));

			/* See pattern.h:pattern_config for description and
			 * pattern.c:DEFAULT_PATTERN_CONFIG for default values
			 * of the following options. */
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
	ps->pc.spat_dict = spatial_dict_init(ps->gen_spat_dict);
	ps->loaded_spatials = ps->pc.spat_dict->nspatials;

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
	e->finish = patternscan_finish;
	e->data = ps;

	return e;
}
