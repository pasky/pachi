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


/* Internal engine state. */
struct patternscan {
	int debug_level;

	struct pattern_config pc;
	pattern_spec ps;
	bool competition;
	bool mm;

	bool no_pattern_match;
	bool gen_spat_dict;
	/* Minimal number of occurences for spatial to be saved;
	 * 3x3 spatials are always saved. */
	int spat_threshold;
	/* Number of loaded spatials; checkpoint for saving new sids
	 * in case gen_spat_dict is enabled. */
	int loaded_spatials;

	/* Book-keeping of spatial occurence count. */
	int nscounts;
	int *scounts;
	/* TODO: The same for PATTERN3 occurences. */
};


/* Starting gamma number of each feature. */
static int gammaid[FEAT_MAX + MAX_PATTERN_DIST + 1];
/* For each spatial id, its gamma value. */
static int spatg[65536];

/* Print MM-format header - summary of features. Also create patterns.fdict
 * containing mapping from gamma numbers to feature:payload pairs. */
static void
mm_header(struct patternscan *ps)
{
	FILE *fdict = fopen("patterns.fdict", "w");
	if (!fdict) {
		perror("patterns.fdict");
		exit(EXIT_FAILURE);
	}

	gammaid[0] = 0;
	int g = 0;
	for (int i = 0; i < FEAT_MAX; i++) {
		if (i == FEAT_SPATIAL) {
			/* Special handling. */
			gammaid[i + 1] = gammaid[i];
			continue;
		}
		gammaid[i + 1] = gammaid[i] + feature_payloads(&ps->pc, i);

		for (int p = 0; p < feature_payloads(&ps->pc, i); p++) {
			struct feature f = { .id = i, .payload = p };
			char buf[256] = ""; feature2str(buf, &f);
			fprintf(fdict, "%d %s\n", g++, buf);
		}
		assert(g == gammaid[i + 1]);
	}

	/* We need to break down spatials by their radius, since payloads
	 * of single feature must be independent. */
	for (int d = 0; d <= ps->pc.spat_max - ps->pc.spat_min; d++) {
		for (int i = 0; i < ps->pc.spat_dict->nspatials; i++) {
			if (ps->pc.spat_dict->spatials[i].dist != ps->pc.spat_min + d)
				continue;
			spatg[i] = g++;
			struct feature f = { .id = FEAT_SPATIAL, .payload = i };
			char buf[256] = ""; feature2str(buf, &f);
			fprintf(fdict, "%d %s\n", spatg[i], buf);
		}
		gammaid[FEAT_MAX + d + 1] = g;
	}

	fclose(fdict);

	int features = FEAT_MAX + ps->pc.spat_max - ps->pc.spat_min + 1;
	printf("! %d\n", gammaid[features]); // Number of gammas.
	printf("%d\n", features - 1); // Number of features; we leave out FEAT_SPATIAL record.
	for (int i = 0; i < FEAT_MAX; i++) {
		if (i == FEAT_SPATIAL) continue;
		// Number of gammas per feature.
		printf("%d %s\n", feature_payloads(&ps->pc, i), feature_name(i));
	}
	for (int d = 0; d <= ps->pc.spat_max - ps->pc.spat_min; d++) {
		printf("%d %s.%d\n", gammaid[FEAT_MAX + d + 1] - gammaid[FEAT_MAX + d], feature_name(FEAT_SPATIAL), ps->pc.spat_min + d);
	}
	printf("!\n");
}

static char *
mm_pattern(struct patternscan *ps, char *str, struct pattern *p)
{
	for (int i = 0; i < p->n; i++) {
		if (i > 0) str = stpcpy(str, " ");
		if (p->f[i].id != FEAT_SPATIAL)
			str += sprintf(str, "%d", gammaid[p->f[i].id] + p->f[i].payload);
		else
			str += sprintf(str, "%d", spatg[p->f[i].payload]);
	}
	return stpcpy(str, "\n");
}


/* FEAT_PATTERN3-matched patterns do not account for rotations
 * and transpositions, but we shold emit only pattern hashes
 * in "canonical" form that is the same no matter the rotation.
 * To achieve this, we use a trick - the canonical form is the
 * one recorded in the spatial dictionary! The dictionary is
 * already closed on rotation and transposition. */
void
pattern_p3_normalize(struct patternscan *ps, struct pattern *p)
{
	for (int i = 0; i < p->n; i++) {
		if (p->f[i].id != FEAT_PATTERN3)
			continue;
		/* Normalize the pattern hash across rotation and
		 * transposition space. */
		p->f[i].payload = pattern3_by_spatial(ps->pc.spat_dict, p->f[i].payload);
	}
}


static void
process_pattern(struct patternscan *ps, struct board *b, struct move *m, char **str)
{
	/* First, store the spatial configuration in dictionary
	 * if applicable. */
	if (ps->gen_spat_dict && !is_pass(m->coord)) {
		struct spatial s;
		spatial_from_board(&ps->pc, &s, b, m);
		int dmax = s.dist;
		for (int d = ps->pc.spat_min; d <= dmax; d++) {
			s.dist = d;
			int sid = spatial_dict_put(ps->pc.spat_dict, &s, spatial_hash(0, &s));
			assert(sid > 0);
			/* Allocate space in 1024 blocks. */
			#define SCOUNTS_ALLOC 1024
			if (sid >= ps->nscounts) {
				int newnsc = (sid / SCOUNTS_ALLOC + 1) * SCOUNTS_ALLOC;
				ps->scounts = realloc(ps->scounts, newnsc * sizeof(*ps->scounts));
				memset(&ps->scounts[ps->nscounts], 0, (newnsc - ps->nscounts) * sizeof(*ps->scounts));
				ps->nscounts = newnsc;
			}
			if (DEBUGL(4) && !ps->scounts[sid]) {
				fprintf(stderr, "new spat %d(%d) %s ", sid, s.dist, spatial2str(&s));
				for (int r = 0; r < 8; r++)
					fprintf(stderr,"[%"PRIhash"] ", spatial_hash(r, &s));
				fprintf(stderr, "\n");
			}
			ps->scounts[sid]++;
		}
	}

	/* Now, match the pattern. */
	if (!ps->no_pattern_match) {
		struct pattern p;
		pattern_match(&ps->pc, ps->ps, &p, b, m);
		pattern_p3_normalize(ps, &p);
		*str = ps->mm ? mm_pattern(ps, *str, &p) : pattern2str(*str, &p);
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
			if (is_pass(mo.coord))
				continue;
#if 0
			/* We want to list again the played move too. This is
			 * required by the MM tool. */
			if (mo.coord == m->coord)
				continue;
#endif
			if (!board_is_valid_move(b, &mo))
				continue;
			if (!ps->mm) *strp++ = ' ';
			process_pattern(ps, b, &mo, &strp);
		}
	}

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
		spatial_dict_writeinfo(ps->pc.spat_dict, f);

	for (int i = ps->loaded_spatials; i < ps->pc.spat_dict->nspatials; i++) {
		/* By default, threshold is 0 and condition is always true. */
		assert(i < ps->nscounts && ps->scounts[i] > 0);
		if (ps->scounts[i] >= ps->spat_threshold
		    || ps->pc.spat_dict->spatials[i].dist == 3)
			spatial_write(ps->pc.spat_dict, &ps->pc.spat_dict->spatials[i], i, f);
	}
	fclose(f);
}


struct patternscan *
patternscan_state_init(char *arg)
{
	struct patternscan *ps = calloc(1, sizeof(struct patternscan));
	int xspat = -1;

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

			} else if (!strcasecmp(optname, "matchfast")) {
				/* Limit the matched features only to the
				 * set used in MC simulations. */
				ps->pc = FAST_PATTERN_CONFIG;
				memcpy(&ps->ps, PATTERN_SPEC_MATCHFAST, sizeof(pattern_spec));

			} else if (!strcasecmp(optname, "mm")) {
				/* Generate output almost suitable for the
				 * Remi Coulom's MM tool, and auxiliar file
				 * "patterns.fdict" with mapping from gamma ids
				 * back to feature,payload pairs. You will need
				 * to post-process the output, substituting
				 * s/\n\n= /#\n/. */
				ps->mm = !optval || atoi(optval);

			} else if (!strcasecmp(optname, "xspat") && optval) {
				/* xspat==0: don't match spatial features
				 * xspat==1: match *only* spatial features */
				xspat = atoi(optval);

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
	for (int i = 0; i < FEAT_MAX; i++) if ((xspat == 0 && i == FEAT_SPATIAL) || (xspat == 1 && i != FEAT_SPATIAL)) ps->ps[i] = 0;
	ps->pc.spat_dict = spatial_dict_init(ps->gen_spat_dict);
	ps->loaded_spatials = ps->pc.spat_dict->nspatials;

	if (ps->mm)
		mm_header(ps);

	return ps;
}

struct engine *
engine_patternscan_init(char *arg, struct board *b)
{
	struct patternscan *ps = patternscan_state_init(arg);
	struct engine *e = calloc(1, sizeof(struct engine));
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
