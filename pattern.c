#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "patternsp.h"
#include "tactics.h"


struct pattern_config DEFAULT_PATTERN_CONFIG = {
	.spat_min = 3, .spat_max = MAX_PATTERN_DIST,
	.bdist_max = 4,
	.ldist_min = 0, .ldist_max = 256,
	.mcsims = 0, /* Unsupported. */
};

struct pattern_config FAST_PATTERN_CONFIG = {
	.spat_min = 3, .spat_max = 3,
	.bdist_max = -1,
	.ldist_min = 0, .ldist_max = 256,
	.mcsims = 0,
};

pattern_spec PATTERN_SPEC_MATCHALL = {
	[FEAT_PASS] = ~0,
	[FEAT_CAPTURE] = ~0,
	[FEAT_AESCAPE] = ~0,
	[FEAT_SELFATARI] = ~0,
	[FEAT_ATARI] = ~0,
	[FEAT_BORDER] = ~0,
	[FEAT_LDIST] = ~0,
	[FEAT_LLDIST] = ~0,
	[FEAT_CONTIGUITY] = ~0,
	[FEAT_SPATIAL] = ~0,
	[FEAT_MCOWNER] = ~0,
};

#define FAST_NO_LADDER 1 /* 1: Don't match ladders in fast playouts */
pattern_spec PATTERN_SPEC_MATCHFAST = {
	[FEAT_PASS] = ~0,
	[FEAT_CAPTURE] = ~(1<<PF_CAPTURE_ATARIDEF | 1<<PF_CAPTURE_RECAPTURE | FAST_NO_LADDER<<PF_CAPTURE_LADDER),
	[FEAT_AESCAPE] = ~(FAST_NO_LADDER<<PF_AESCAPE_LADDER),
	[FEAT_SELFATARI] = ~(1<<PF_SELFATARI_SMART),
	[FEAT_ATARI] = ~(FAST_NO_LADDER<<PF_ATARI_LADDER),
	[FEAT_BORDER] = 0,
	[FEAT_LDIST] = 0,
	[FEAT_LLDIST] = 0,
	[FEAT_CONTIGUITY] = ~0,
	[FEAT_SPATIAL] = ~0,
	[FEAT_MCOWNER] = ~0,
};

static const struct feature_info {
	char *name;
	int payloads;
} features[FEAT_MAX] = {
	[FEAT_PASS] = { .name = "pass", .payloads = 2 },
	[FEAT_CAPTURE] = { .name = "capture", .payloads = 16 },
	[FEAT_AESCAPE] = { .name = "atariescape", .payloads = 2 },
	[FEAT_SELFATARI] = { .name = "selfatari", .payloads = 2 },
	[FEAT_ATARI] = { .name = "atari", .payloads = 4 },
	[FEAT_BORDER] = { .name = "border", .payloads = -1 },
	[FEAT_LDIST] = { .name = "ldist", .payloads = -1 },
	[FEAT_LLDIST] = { .name = "lldist", .payloads = -1 },
	[FEAT_CONTIGUITY] = { .name = "cont", .payloads = 2 },
	[FEAT_SPATIAL] = { .name = "s", .payloads = -1 },
	[FEAT_MCOWNER] = { .name = "mcowner", .payloads = 16 },
};

char *
feature2str(char *str, struct feature *f)
{
	return str + sprintf(str + strlen(str), "%s:%d", features[f->id].name, f->payload);
}

char *
str2feature(char *str, struct feature *f)
{
	while (isspace(*str)) str++;

	int flen = strcspn(str, ":");
	for (int i = 0; i < sizeof(features)/sizeof(features[0]); i++)
		if (strlen(features[i].name) == flen && !strncmp(features[i].name, str, flen)) {
			f->id = i;
			goto found;
		}
	fprintf(stderr, "invalid featurespec: %s[%d]\n", str, flen);
	exit(EXIT_FAILURE);

found:
	str += flen + 1;
	f->payload = strtoull(str, &str, 10);
	return str;
}


/* pattern_spec helpers */
#define PS_ANY(F) (ps[FEAT_ ## F] & (1 << 31))
#define PS_PF(F, P) (ps[FEAT_ ## F] & (1 << PF_ ## F ## _ ## P))

static struct feature *
pattern_match_capture(struct pattern_config *pc, pattern_spec ps,
                      struct pattern *p, struct feature *f,
                      struct board *b, struct move *m)
{
	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != stone_other(m->color))
			continue;
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 1)
			continue;

		/* Capture! */
		f->id = FEAT_CAPTURE; f->payload = 0;

		if (PS_PF(CAPTURE, LADDER))
			f->payload |= is_ladder(b, m->coord, g, true, true) << PF_CAPTURE_LADDER;
		/* TODO: is_ladder() is too conservative in some
		 * very obvious situations, look at complete.gtp. */

		/* TODO: PF_CAPTURE_RECAPTURE */

		if (PS_PF(CAPTURE, ATARIDEF))
		foreach_in_group(b, g) {
			foreach_neighbor(b, c, {
				assert(board_at(b, c) != S_NONE || c == m->coord);
				if (board_at(b, c) != m->color)
					continue;
				group_t g = group_at(b, c);
				if (!g || board_group_info(b, g).libs != 1)
					continue;
				/* A neighboring group of ours is in atari. */
				f->payload |= 1 << PF_CAPTURE_ATARIDEF;
			});
		} foreach_in_group_end;

		if (PS_PF(CAPTURE, KO)
		    && group_is_onestone(b, g)
		    && neighbor_count_at(b, m->coord, stone_other(m->color))
		       + neighbor_count_at(b, m->coord, S_OFFBOARD) == 4)
			f->payload |= 1 << PF_CAPTURE_KO;

		(f++, p->n++);
	});
	return f;
}

static struct feature *
pattern_match_aescape(struct pattern_config *pc, pattern_spec ps,
                      struct pattern *p, struct feature *f,
		      struct board *b, struct move *m)
{
	/* Find if a neighboring group of ours is in atari, AND that we provide
	 * a liberty to connect out. XXX: No connect-and-die check. */
	group_t in_atari = -1;
	bool has_extra_lib = false;
	int payload = 0;

	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != m->color) {
			if (board_at(b, c) == S_NONE)
				has_extra_lib = true; // free point
			else if (board_at(b, c) == stone_other(m->color) && board_group_info(b, group_at(b, c)).libs == 1)
				has_extra_lib = true; // capturable enemy group
			continue;
		}
		group_t g = group_at(b, c); assert(g);
		if (board_group_info(b, g).libs != 1) {
			has_extra_lib = true;
			continue;
		}

		/* In atari! */
		in_atari = g;

		if (PS_PF(AESCAPE, LADDER))
			payload |= is_ladder(b, m->coord, g, true, true) << PF_AESCAPE_LADDER;
		/* TODO: is_ladder() is too conservative in some
		 * very obvious situations, look at complete.gtp. */
	});
	if (in_atari >= 0 && has_extra_lib) {
		f->id = FEAT_AESCAPE; f->payload = payload;
		(f++, p->n++);
	}
	return f;
}

static struct feature *
pattern_match_atari(struct pattern_config *pc, pattern_spec ps,
                    struct pattern *p, struct feature *f,
		    struct board *b, struct move *m)
{
	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != stone_other(m->color))
			continue;
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs != 2)
			continue;

		/* Can atari! */
		f->id = FEAT_ATARI; f->payload = 0;

		if (PS_PF(ATARI, LADDER)) {
			/* Opponent will escape by the other lib. */
			coord_t lib = board_group_info(b, g).lib[0];
			if (lib == m->coord) lib = board_group_info(b, g).lib[1];
			/* TODO: is_ladder() is too conservative in some
			 * very obvious situations, look at complete.gtp. */
			f->payload |= is_ladder(b, lib, g, true, true) << PF_ATARI_LADDER;
		}

		if (PS_PF(ATARI, KO) && !is_pass(b->ko.coord))
			f->payload |= 1 << PF_ATARI_KO;

		(f++, p->n++);
	});
	return f;
}

#ifndef BOARD_SPATHASH
#undef BOARD_SPATHASH_MAXD
#define BOARD_SPATHASH_MAXD 1
#endif

/* Match spatial features that are too distant to be pre-matched
 * incrementally. */
struct feature *
pattern_match_spatial_outer(struct pattern_config *pc, pattern_spec ps,
                            struct pattern *p, struct feature *f,
		            struct board *b, struct move *m, hash_t h)
{
	/* We record all spatial patterns black-to-play; simply
	 * reverse all colors if we are white-to-play. */
	static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
	static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	enum stone (*bt)[4] = m->color == S_WHITE ? &bt_white : &bt_black;

	for (int d = BOARD_SPATHASH_MAXD + 1; d <= pc->spat_max; d++) {
		/* Recompute missing outer circles:
		 * Go through all points in given distance. */
		for (int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, m->coord, b, j);
			h ^= pthashes[0][j][(*bt)[board_atxy(b, x, y)]];
		}
		if (d < pc->spat_min)
			continue;
		/* Record spatial feature, one per distance. */
		int sid = spatial_dict_get(pc->spat_dict, d, h & spatial_hash_mask);
		if (sid > 0) {
			f->id = FEAT_SPATIAL;
			f->payload = sid;
			(f++, p->n++);
		} /* else not found, ignore */
	}
	return f;
}

struct feature *
pattern_match_spatial(struct pattern_config *pc, pattern_spec ps,
                      struct pattern *p, struct feature *f,
		      struct board *b, struct move *m)
{
	/* XXX: This is partially duplicated from spatial_from_board(), but
	 * we build a hash instead of spatial record. */

	assert(pc->spat_min > 0);

	bool w_to_play = m->color == S_WHITE;
	hash_t h = pthashes[0][0][S_NONE];
	for (int d = 2; d <= BOARD_SPATHASH_MAXD; d++) {
		/* Reuse all incrementally matched data. */
		h ^= b->spathash[m->coord][d - 1][w_to_play];
		if (d < pc->spat_min)
			continue;
		/* Record spatial feature, one per distance. */
		int sid = spatial_dict_get(pc->spat_dict, d, h & spatial_hash_mask);
		if (sid > 0) {
			f->id = FEAT_SPATIAL;
			f->payload = sid;
			(f++, p->n++);
		} /* else not found, ignore */
	}
	if (unlikely(pc->spat_max > BOARD_SPATHASH_MAXD))
		f = pattern_match_spatial_outer(pc, ps, p, f, b, m, h);
	return f;
}


static bool
is_simple_selfatari(struct board *b, enum stone color, coord_t coord)
{
	/* Very rough check, no connect-and-die checks or other trickery. */
	int libs = immediate_liberty_count(b, coord);
	if (libs >= 2) return false; // open space

	group_t seen = -1;
	foreach_neighbor(b, coord, {
		if (board_at(b, c) == stone_other(color) && board_group_info(b, group_at(b, c)).libs == 1) {
			return false; // can capture

		} else if (board_at(b, c) == color) {
			// friendly group, does it have liberties?
			group_t g = group_at(b, c);
			if (board_group_info(b, g).libs == 1 || seen == g)
				continue;
			libs += board_group_info(b, g).libs - 1;
			if (libs >= 2) return false;
			// don't consider the same group twice
			seen = g;
		}
	});
	return true;
}

void
pattern_match(struct pattern_config *pc, pattern_spec ps,
              struct pattern *p, struct board *b, struct move *m)
{
	p->n = 0;
	struct feature *f = &p->f[0];

	/* TODO: We should match pretty much all of these features
	 * incrementally. */

	if (is_pass(m->coord)) {
		if (PS_ANY(PASS)) {
			f->id = FEAT_PASS; f->payload = 0;
			if (PS_PF(PASS, LASTPASS))
				f->payload |= (b->moves > 0 && is_pass(b->last_move.coord))
						<< PF_PASS_LASTPASS;
			p->n++;
		}
		return;
	}

	if (PS_ANY(CAPTURE)) {
		f = pattern_match_capture(pc, ps, p, f, b, m);
	}

	if (PS_ANY(AESCAPE)) {
		f = pattern_match_aescape(pc, ps, p, f, b, m);
	}

	if (PS_ANY(SELFATARI)) {
		bool simple = is_simple_selfatari(b, m->color, m->coord);
		bool thorough = false;
		if (PS_PF(SELFATARI, SMART)) {
			thorough = is_bad_selfatari(b, m->color, m->coord);
		}
		if (simple || thorough) {
			f->id = FEAT_SELFATARI;
			f->payload = thorough << PF_SELFATARI_SMART;
			(f++, p->n++);
		}
	}

	if (PS_ANY(ATARI)) {
		f = pattern_match_atari(pc, ps, p, f, b, m);
	}

	if (PS_ANY(BORDER)) {
		int bdist = coord_edge_distance(m->coord, b);
		if (bdist <= pc->bdist_max) {
			f->id = FEAT_BORDER;
			f->payload = bdist;
			(f++, p->n++);
		}
	}

	if (PS_ANY(LDIST) && pc->ldist_max > 0 && !is_pass(b->last_move.coord)) {
		int ldist = coord_gridcular_distance(m->coord, b->last_move.coord, b);
		if (pc->ldist_min <= ldist && ldist <= pc->ldist_max) {
			f->id = FEAT_LDIST;
			f->payload = ldist;
			(f++, p->n++);
		}
	}

	if (PS_ANY(CONTIGUITY) && !is_pass(b->last_move.coord)
	    && coord_is_8adjecent(m->coord, b->last_move.coord, b)) {
		f->id = FEAT_CONTIGUITY;
		f->payload = 1;
		(f++, p->n++);
	}

	if (PS_ANY(LLDIST) && pc->ldist_max > 0 && !is_pass(b->last_move2.coord)) {
		int lldist = coord_gridcular_distance(m->coord, b->last_move2.coord, b);
		if (pc->ldist_min <= lldist && lldist <= pc->ldist_max) {
			f->id = FEAT_LLDIST;
			f->payload = lldist;
			(f++, p->n++);
		}
	}

	if (PS_ANY(SPATIAL) && pc->spat_max > 0 && pc->spat_dict) {
		f = pattern_match_spatial(pc, ps, p, f, b, m);
	}

	/* FEAT_MCOWNER: TODO */
	assert(!pc->mcsims);
}

char *
pattern2str(char *str, struct pattern *p)
{
	str = stpcpy(str, "(");
	for (int i = 0; i < p->n; i++) {
		if (i > 0) str = stpcpy(str, " ");
		str = feature2str(str, &p->f[i]);
	}
	str = stpcpy(str, ")");
	return str;
}



/*** Features gamma set */

static void
features_gamma_load(struct features_gamma *fg, const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f) return;
	char buf[256];
	while (fgets(buf, 256, f)) {
		char *bufp = buf;
		struct feature f;
		bufp = str2feature(bufp, &f);
		while (isspace(*bufp)) bufp++;
		float gamma = strtof(bufp, &bufp);
		feature_gamma(fg, &f, &gamma);
	}
	fclose(f);
}

const char *features_gamma_filename = "patterns.gamma";

struct features_gamma *
features_gamma_init(struct pattern_config *pc, const char *file)
{
	struct features_gamma *fg = calloc(1, sizeof(*fg));
	fg->pc = pc;
	for (int i = 0; i < FEAT_MAX; i++) {
		int n = features[i].payloads;
		if (n <= 0) {
			switch (i) {
				case FEAT_SPATIAL:
					n = pc->spat_dict->nspatials; break;
				case FEAT_LDIST:
				case FEAT_LLDIST:
					n = pc->ldist_max + 1; break;
				case FEAT_BORDER:
					n = pc->bdist_max + 1; break;
				default:
					assert(0);
			}
		}
		fg->gamma[i] = malloc(n * sizeof(float));
		for (int j = 0; j < n; j++) {
			fg->gamma[i][j] = 1.0f;
		}
	}
	features_gamma_load(fg, file ? file : features_gamma_filename);
	return fg;
}
