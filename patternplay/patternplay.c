#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "patternplay/patternplay.h"
#include "pattern.h"
#include "patternsp.h"
#include "patternprob.h"
#include "random.h"


/* Internal engine state. */
struct patternplay {
	int debug_level;

	struct pattern_config pc;
	pattern_spec ps;
	struct pattern_pdict *pd;
};


static coord_t *
patternplay_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	struct patternplay *pp = e->data;

	struct pattern pats[b->flen];
	floating_t probs[b->flen];
	pattern_rate_moves(&pp->pc, &pp->ps, pp->pd, b, color, pats, probs);

	int best = 0;
	for (int f = 0; f < b->flen; f++) {
		if (pp->debug_level >= 5 && probs[f] >= 0.001) {
			char s[256]; pattern2str(s, &pats[f]);
			fprintf(stderr, "\t%s: %.3f %s\n", coord2sstr(b->f[f], b), probs[f], s);
		}
		if (probs[f] > probs[best])
			best = f;
	}

	return coord_copy(b->f[best]);
}

void
patternplay_evaluate(struct engine *e, struct board *b, struct time_info *ti, floating_t *vals, enum stone color)
{
	struct patternplay *pp = e->data;

	struct pattern pats[b->flen];
	floating_t total = pattern_rate_moves(&pp->pc, &pp->ps, pp->pd, b, color, pats, vals);

#if 0
	/* Rescale properly. */
	for (int f = 0; f < b->flen; f++) {
		probs[f] /= total;
	}
#endif

	if (pp->debug_level >= 4) {
		for (int f = 0; f < b->flen; f++) {
			if (vals[f] >= 0.001) {
				char s[256]; pattern2str(s, &pats[f]);
				fprintf(stderr, "\t%s: %.3f %s\n", coord2sstr(b->f[f], b), vals[f], s);
			}
		}
	}
}


struct patternplay *
patternplay_state_init(char *arg)
{
	struct patternplay *pp = calloc2(1, sizeof(struct patternplay));

	pp->debug_level = debug_level;
	pp->pc = DEFAULT_PATTERN_CONFIG;
	pp->pc.spat_dict = spatial_dict_init(false, false);
	memcpy(&pp->ps, PATTERN_SPEC_MATCH_DEFAULT, sizeof(pattern_spec));

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
					pp->debug_level = atoi(optval);
				else
					pp->debug_level++;

			/* See pattern.h:pattern_config for description and
			 * pattern.c:DEFAULT_PATTERN_CONFIG for default values
			 * of the following options. */
			} else if (!strcasecmp(optname, "bdist_max") && optval) {
				pp->pc.bdist_max = atoi(optval);
			} else if (!strcasecmp(optname, "spat_min") && optval) {
				pp->pc.spat_min = atoi(optval);
			} else if (!strcasecmp(optname, "spat_max") && optval) {
				pp->pc.spat_max = atoi(optval);
			} else if (!strcasecmp(optname, "spat_largest")) {
				pp->pc.spat_largest = !optval || atoi(optval);

			} else {
				fprintf(stderr, "patternplay: Invalid engine argument %s or missing value\n", optname);
				exit(EXIT_FAILURE);
			}
		}
	}

	pp->pd = pattern_pdict_init(NULL, &pp->pc);

	return pp;
}

struct engine *
engine_patternplay_init(char *arg, struct board *b)
{
	struct patternplay *pp = patternplay_state_init(arg);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "PatternPlay Engine";
	e->comment = "I select moves blindly according to learned patterns. I won't pass as long as there is a place on the board where I can play. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = patternplay_genmove;
	e->evaluate = patternplay_evaluate;
	e->data = pp;

	return e;
}
