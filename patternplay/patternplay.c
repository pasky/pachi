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

	struct pattern_setup pat;
};


static coord_t *
patternplay_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	struct patternplay *pp = e->data;

	struct pattern pats[b->flen];
	floating_t probs[b->flen];
	pattern_rate_moves(&pp->pat, b, color, pats, probs);

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
	pattern_rate_moves(&pp->pat, b, color, pats, vals);

#if 0
	// unused variable 'total' in above call to pattern_rate_moves()
	floating_t total = pattern_rate_moves(&pp->pat, b, color, pats, vals);
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
	bool pat_setup = false;

	pp->debug_level = debug_level;

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

			} else if (!strcasecmp(optname, "patterns") && optval) {
				patterns_init(&pp->pat, optval, false, true);
				pat_setup = true;

			} else {
				fprintf(stderr, "patternplay: Invalid engine argument %s or missing value\n", optname);
				exit(EXIT_FAILURE);
			}
		}
	}

	if (!pat_setup)
		patterns_init(&pp->pat, NULL, false, true);

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
