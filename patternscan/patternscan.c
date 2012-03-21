#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "patternscan/patternscan.h"
#include "pattern.h"
#include "patternsp.h"
#include "random.h"


/* The engine has two modes:
 *
 * * gen_spat_dict=1: patterns.spat file is generated with a list of all
 *   encountered spatials
 *
 * * gen_spat_dict=0,no_pattern_match=1: all encountered patterns are
 *   listed on output on each move; the general format is
 * 	[(winpattern)]
 *   but with competition=1 it is
 * 	[(winpattern)] [(witnesspattern0) (witnesspattern1) ...]
 *   and with spat_split_sizes=1 even
 * 	[(winpattern0) (winpattern1) ...] [(witpattern0) (witpattern1) ...]
 */


/* Internal engine state. */
struct patternscan {
	int debug_level;

	struct pattern_setup pat;
	bool competition;
	bool spat_split_sizes;
	int color_mask;

	bool no_pattern_match;
	bool gen_spat_dict;
	/* Minimal number of occurences for spatial to be saved. */
	int spat_threshold;
	/* Number of loaded spatials; checkpoint for saving new sids
	 * in case gen_spat_dict is enabled. */
	int loaded_spatials;

	/* Book-keeping of spatial occurence count. */
	int gameno;
	unsigned int nscounts;
	int *scounts;
	int *sgameno;
};


static void
process_pattern(struct patternscan *ps, struct board *b, struct move *m, char **str)
{
	/* First, store the spatial configuration in dictionary
	 * if applicable. */
	if (ps->gen_spat_dict && !is_pass(m->coord)) {
		struct spatial s;
		spatial_from_board(&ps->pat.pc, &s, b, m);
		int dmax = s.dist;
		for (int d = ps->pat.pc.spat_min; d <= dmax; d++) {
			s.dist = d;
			unsigned int sid = spatial_dict_put(ps->pat.pc.spat_dict, &s, spatial_hash(0, &s));
			assert(sid > 0);
			#define SCOUNTS_ALLOC 1048576 // Allocate space in 1M*4 blocks.
			if (sid >= ps->nscounts) {
				int newnsc = (sid / SCOUNTS_ALLOC + 1) * SCOUNTS_ALLOC;
				ps->scounts = realloc(ps->scounts, newnsc * sizeof(*ps->scounts));
				memset(&ps->scounts[ps->nscounts], 0, (newnsc - ps->nscounts) * sizeof(*ps->scounts));
				ps->sgameno = realloc(ps->sgameno, newnsc * sizeof(*ps->sgameno));
				memset(&ps->sgameno[ps->nscounts], 0, (newnsc - ps->nscounts) * sizeof(*ps->sgameno));
				ps->nscounts = newnsc;
			}
			if (ps->debug_level > 1 && !fast_random(65536) && !fast_random(32)) {
				fprintf(stderr, "%d spatials, %d collisions\n", ps->pat.pc.spat_dict->nspatials, ps->pat.pc.spat_dict->collisions);
			}
			if (ps->sgameno[sid] != ps->gameno) {
				ps->scounts[sid]++;
				ps->sgameno[sid] = ps->gameno;
			}
		}
	}

	/* Now, match the pattern. */
	if (!ps->no_pattern_match) {
		struct pattern p;
		pattern_match(&ps->pat.pc, ps->pat.ps, &p, b, m);

		if (!ps->spat_split_sizes) {
			*str = pattern2str(*str, &p);
		} else {
			/* XXX: We assume that FEAT_SPATIAL items
			 * are at the end. */
			struct pattern p2;
			int i = 0;
			while (i < p.n && p.f[i].id != FEAT_SPATIAL) {
				p2.f[i] = p.f[i];
				i++;
			}
			if (i == p.n) {
				p2.n = i;
				*str = pattern2str(*str, &p2);
			} else {
				p2.n = i + 1;
				for (int j = i; j < p.n; j++) {
					assert(p.f[j].id == FEAT_SPATIAL);
					p2.f[i] = p.f[j];
					if ((*str)[-1] == ')')
						*(*str)++ = ' ';
					*str = pattern2str(*str, &p2);
				}
			}
		}
	}
}

static char *
patternscan_play(struct engine *e, struct board *b, struct move *m, char *enginearg)
{
	struct patternscan *ps = e->data;

	if (is_resign(m->coord))
		return NULL;
	/* Deal with broken game records that sometimes get fed in. */
	if (board_at(b, m->coord) != S_NONE)
		return NULL;

	if (b->moves == (b->handicap ? b->handicap * 2 : 1))
		ps->gameno++;

	if (!(m->color & ps->color_mask))
		return NULL;
	/* The user can request this play to be "silent", to get patterns
	 * only for a single specific situation. */
	if (enginearg && *enginearg == '0')
		return NULL;

	static char str[1048576]; // XXX
	char *strp = str;
	*str = 0;

	/* Scan for supported features. */
	/* For specifiation of features and their payloads,
	 * please refer to pattern.h. */
	*strp++ = '[';
	process_pattern(ps, b, m, &strp);
	*strp++ = ']';

	if (ps->competition) {
		/* Look at other possible moves as well. */
		*strp++ = ' ';
		*strp++ = '[';
		for (int f = 0; f < b->flen; f++) {
			struct move mo = { .coord = b->f[f], .color = m->color };
			if (is_pass(mo.coord))
				continue;
			if (!board_is_valid_move(b, &mo))
				continue;
			if (strp[-1] != '[')
				*strp++ = ' ';
			process_pattern(ps, b, &mo, &strp);
		}
		*strp++ = ']';
	}
	*strp++ = 0;

	return ps->no_pattern_match ? NULL : str;
}

static coord_t *
patternscan_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	fprintf(stderr, "genmove command not available during patternscan!\n");
	exit(EXIT_FAILURE);
}

void
patternscan_done(struct engine *e)
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
		spatial_dict_writeinfo(ps->pat.pc.spat_dict, f);

	for (unsigned int i = ps->loaded_spatials; i < ps->pat.pc.spat_dict->nspatials; i++) {
		/* By default, threshold is 0 and condition is always true. */
		assert(i < ps->nscounts && ps->scounts[i] > 0);
		if (ps->scounts[i] >= ps->spat_threshold)
			spatial_write(ps->pat.pc.spat_dict, &ps->pat.pc.spat_dict->spatials[i], i, f);
	}
	fclose(f);
}


struct patternscan *
patternscan_state_init(char *arg)
{
	struct patternscan *ps = calloc2(1, sizeof(struct patternscan));
	bool pat_setup = false;
	int xspat = -1;

	ps->debug_level = 1;
	ps->color_mask = S_BLACK | S_WHITE;

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
				/* XXX: If you specify the 'patterns' option,
				 * this must come first! */
				ps->gen_spat_dict = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "no_pattern_match")) {
				/* If set, do not actually match patterns.
				 * Useful only together with gen_spat_dict
				 * when just building spatial dictionary. */
				ps->no_pattern_match = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "spat_threshold") && optval) {
				/* Minimal number of times new spatial
				 * feature must occur in this run (!) to
				 * be included in the dictionary. Note that
				 * this will produce discontinuous dictionary
				 * that you should renumber. Also note that
				 * 3x3 patterns are always saved. */
				ps->spat_threshold = atoi(optval);

			} else if (!strcasecmp(optname, "competition")) {
				/* In competition mode, first the played
				 * pattern is printed, then all patterns
				 * that could be played (including the played
				 * one). */
				ps->competition = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "spat_split_sizes")) {
				/* Generate a separate pattern for each
				 * spatial size. This is important to
				 * preserve good generalization in unknown
				 * situations where the largest pattern
				 * might not match. */
				ps->spat_split_sizes = 1;

			} else if (!strcasecmp(optname, "color_mask") && optval) {
				/* Bitmask of move colors to match. Set this
				 * to 2 if you want to match only white moves,
				 * for example. (Useful for processing
				 * handicap games.) */
				ps->color_mask = atoi(optval);

			} else if (!strcasecmp(optname, "xspat") && optval) {
				/* xspat==0: don't match spatial features
				 * xspat==1: match *only* spatial features */
				xspat = atoi(optval);

			} else if (!strcasecmp(optname, "patterns") && optval) {
				patterns_init(&ps->pat, optval, ps->gen_spat_dict, false);
				pat_setup = true;

			} else {
				fprintf(stderr, "patternscan: Invalid engine argument %s or missing value\n", optname);
				exit(EXIT_FAILURE);
			}
		}
	}

	if (!pat_setup)
		patterns_init(&ps->pat, NULL, ps->gen_spat_dict, false);
	if (ps->spat_split_sizes)
		ps->pat.pc.spat_largest = 0;

	for (int i = 0; i < FEAT_MAX; i++) if ((xspat == 0 && i == FEAT_SPATIAL) || (xspat == 1 && i != FEAT_SPATIAL)) ps->pat.ps[i] = 0;
	ps->loaded_spatials = ps->pat.pc.spat_dict->nspatials;

	ps->gameno = 1;

	return ps;
}

struct engine *
engine_patternscan_init(char *arg, struct board *b)
{
	struct patternscan *ps = patternscan_state_init(arg);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "PatternScan Engine";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of games fed to it as GTP streams for various pattern features.";
	e->genmove = patternscan_genmove;
	e->notify_play = patternscan_play;
	e->done = patternscan_done;
	e->data = ps;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;

	return e;
}
