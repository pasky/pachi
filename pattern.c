#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "tactics.h"


struct pattern_config DEFAULT_PATTERN_CONFIG = {
	/* 2..MAX_PATTERN_DIST are absolute spat limits, but according
	 * to CrazyStone we use 3..10 instead to limit pattern amount. */
	.spat_min = 3, .spat_max = 10 /*MAX_PATTERN_DIST*/,
	.bdist_max = 4,
	.ldist_min = 0, .ldist_max = 256,
	.mcsims = 0, /* Unsupported. */
};

struct pattern_config FAST_PATTERN_CONFIG = {
	.spat_min = 3, .spat_max = 5,
	.bdist_max = 4,
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
	[FEAT_SPATIAL] = ~0,
	[FEAT_MCOWNER] = ~0,
};

pattern_spec PATTERN_SPEC_MATCHFAST = {
	[FEAT_PASS] = ~0,
	[FEAT_CAPTURE] = ~(1<<PF_CAPTURE_ATARIDEF)|~(1<<PF_CAPTURE_RECAPTURE),
	[FEAT_AESCAPE] = ~0,
	[FEAT_SELFATARI] = ~(1<<PF_SELFATARI_SMART),
	[FEAT_ATARI] = ~0,
	[FEAT_BORDER] = ~0,
	[FEAT_LDIST] = ~0,
	[FEAT_LLDIST] = ~0,
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
	[FEAT_BORDER] = { .name = "border", .payloads = 1 },
	[FEAT_LDIST] = { .name = "ldist", .payloads = -1 },
	[FEAT_LLDIST] = { .name = "lldist", .payloads = -1 },
	[FEAT_SPATIAL] = { .name = "s", .payloads = -1 },
	[FEAT_MCOWNER] = { .name = "mcowner", .payloads = 16 },
};

char *
feature2str(char *str, struct feature *f)
{
	return str + sprintf(str + strlen(str), "%s:%"PRIx32, features[f->id].name, f->payload);
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
	f->payload = strtoull(str, &str, 16);
	return str;
}


/* Mapping from point sequence to coordinate offsets (to determine
 * coordinates relative to pattern center). The array is ordered
 * in the gridcular metric order so that we can go through it
 * and incrementally match spatial features in nested circles.
 * Within one circle, coordinates are ordered by rows to keep
 * good cache behavior. */
static struct { short x, y; } ptcoords[MAX_PATTERN_AREA];
/* For each radius, starting index in ptcoords[]. */
static int ptind[MAX_PATTERN_DIST + 1];
static void __attribute__((constructor)) ptcoords_init(void)
{
	int i = 0; /* Indexing ptcoords[] */

	/* First, center point. */
	ptind[0] = ptind[1] = 0;
	ptcoords[i].x = ptcoords[i].y = 0; i++;

	for (int d = 2; d < MAX_PATTERN_DIST + 1; d++) {
		ptind[d] = i;
		/* For each y, examine all integer solutions
		 * of d = |x| + |y| + max(|x|, |y|). */
		/* TODO: (Stern, 2006) uses a hand-modified
		 * circles that are finer for small d. */
		for (short y = d / 2; y >= 0; y--) {
			short x;
			if (y > d / 3) {
				/* max(|x|, |y|) = |y|, non-zero x */
				x = d - y * 2;
				if (x + y * 2 != d) continue;
			} else {
				/* max(|x|, |y|) = |x| */
				/* Or, max(|x|, |y|) = |y| and x is zero */
				x = (d - y) / 2;
				if (x * 2 + y != d) continue;
			}

			assert((x > y ? x : y) + x + y == d);

			ptcoords[i].x = x; ptcoords[i].y = y; i++;
			if (x != 0) { ptcoords[i].x = -x; ptcoords[i].y = y; i++; }
			if (y != 0) { ptcoords[i].x = x; ptcoords[i].y = -y; i++; }
			if (x != 0 && y != 0) { ptcoords[i].x = -x; ptcoords[i].y = -y; i++; }
		}
	}
	ptind[MAX_PATTERN_DIST] = i;

#if 0
	for (int d = 0; d < MAX_PATTERN_DIST; d++) {
		fprintf(stderr, "d=%d (%d) ", d, ptind[d]);
		for (int j = ptind[d]; j < ptind[d + 1]; j++) {
			fprintf(stderr, "%d,%d ", ptcoords[j].x, ptcoords[j].y);
		}
		fprintf(stderr, "\n");
	}
#endif
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


/* We record all spatial patterns black-to-play; simply
 * reverse all colors if we are white-to-play. */
static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };

static inline hash_t spatial_hash_one(int rotation, int i, enum stone color);
static struct feature *
pattern_match_spatial(struct pattern_config *pc, pattern_spec ps,
                      struct pattern *p, struct feature *f,
		      struct board *b, struct move *m)
{
	/* XXX: This is partially duplicated from spatial_from_board(), but
	 * building a hash instead of spatial record. */

	assert(pc->spat_min > 0);

	enum stone (*bt)[4] = m->color == S_WHITE ? &bt_white : &bt_black;

	struct spatial s = { .points = {0} };
	hash_t h = spatial_hash_one(0, 0, S_NONE);
	for (int d = 2; d < pc->spat_max; d++) {
		/* Go through all points in given distance. */
		for (int j = ptind[d]; j < ptind[d + 1]; j++) {
			int x = coord_x(m->coord, b) + ptcoords[j].x;
			int y = coord_y(m->coord, b) + ptcoords[j].y;
			if (x >= board_size(b)) x = board_size(b) - 1; else if (x < 0) x = 0;
			if (y >= board_size(b)) y = board_size(b) - 1; else if (y < 0) y = 0;
			/* Append point. */
			s.points[j / 4] |= (*bt)[board_atxy(b, x, y)] << ((j % 4) * 2);
			h ^= spatial_hash_one(0, j, (*bt)[board_atxy(b, x, y)]);
		}
		if (d < pc->spat_min)
			continue;
		/* Record spatial feature, one per distance. */
		f->id = FEAT_SPATIAL;
		f->payload = (d << PF_SPATIAL_RADIUS);
		s.dist = d;
		int sid = spatial_dict_get(pc->spat_dict, &s, h & spatial_hash_mask);
		if (sid > 0) {
			f->payload |= sid << PF_SPATIAL_INDEX;
			(f++, p->n++);
		} /* else not found, ignore */
	}
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
features_gamma_load(struct features_gamma *fg, char *filename)
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

struct features_gamma *
features_gamma_init(struct pattern_config *pc)
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
					n = pc->ldist_max; break;
				default:
					assert(0);
			}
		}
		fg->gamma[i] = malloc(n * sizeof(float));
		for (int j = 0; j < n; j++) {
			fg->gamma[i][j] = 1.0f;
		}
	}
	features_gamma_load(fg, "patterns.gamma");
	return fg;
}

float
feature_gamma(struct features_gamma *fg, struct feature *f, float *gamma)
{
	/* XXX: We mask out spatial distance unconditionally since it shouldn't
	 * affect any other feature. */
	int payid = f->payload & ((1<<24)-1);
	if (gamma) fg->gamma[f->id][payid] = *gamma;
	return fg->gamma[f->id][payid];
}



/*** Spatial patterns dictionary */

/* Zobrist hashes used for black/white stones in patterns. */
#define PTH__ROTATIONS	8
static hash_t pthashes[PTH__ROTATIONS][MAX_PATTERN_AREA][4];
static void __attribute__((constructor)) pthashes_init(void)
{
	/* We need fixed hashes for all pattern-relative in
	 * all pattern users! This is a simple way to generate
	 * hopefully good ones. Park-Miller powa. :) */

	/* We create a virtual board (centered at the sequence start),
	 * plant the hashes there, then pick them up into the sequence
	 * with correct coordinates. It would be possible to generate
	 * the sequence point hashes directly, but the rotations would
	 * make for enormous headaches. */
	hash_t pthboard[MAX_PATTERN_AREA][4];
	int pthbc = MAX_PATTERN_AREA / 2; // tengen coord

	/* The magic numbers are tuned for minimal collisions. */
	hash_t h = 0x313131;
	for (int i = 0; i < MAX_PATTERN_AREA; i++) {
		pthboard[i][S_NONE] = (h = h * 16803 - 7);
		pthboard[i][S_BLACK] = (h = h * 16805 + 7);
		pthboard[i][S_WHITE] = (h = h * 16807 + 3);
		pthboard[i][S_OFFBOARD] = (h = h * 16809 - 3);
	}

	/* Virtual board with hashes created, now fill
	 * pthashes[] with hashes for points in actual
	 * sequences, also considering various rotations. */
#define PTH_VMIRROR	1
#define PTH_HMIRROR	2
#define PTH_90ROT	4
	for (int r = 0; r < PTH__ROTATIONS; r++) {
		for (int i = 0; i < MAX_PATTERN_AREA; i++) {
			/* Rotate appropriately. */
			int rx = ptcoords[i].x;
			int ry = ptcoords[i].y;
			if (r & PTH_VMIRROR) ry = -ry;
			if (r & PTH_HMIRROR) rx = -rx;
			if (r & PTH_90ROT) {
				int rs = rx; rx = -ry; ry = rs;
			}
			int bi = pthbc + ry * MAX_PATTERN_DIST + rx;

			/* Copy info. */
			pthashes[r][i][S_NONE] = pthboard[bi][S_NONE];
			pthashes[r][i][S_BLACK] = pthboard[bi][S_BLACK];
			pthashes[r][i][S_WHITE] = pthboard[bi][S_WHITE];
			pthashes[r][i][S_OFFBOARD] = pthboard[bi][S_OFFBOARD];
		}
	}
}

static inline hash_t
spatial_hash_one(int rotation, int i, enum stone color)
{
	return pthashes[rotation][i][color];
}

inline hash_t
spatial_hash(int rotation, struct spatial *s)
{
	hash_t h = 0;
	for (int i = 0; i < ptind[s->dist + 1]; i++) {
		h ^= pthashes[rotation][i][spatial_point_at(*s, i)];
	}
	return h & spatial_hash_mask;
}

static int
spatial_dict_addc(struct spatial_dict *dict, struct spatial *s)
{
	/* Allocate space in 1024 blocks. */
#define SPATIALS_ALLOC 1024
	if (!(dict->nspatials % SPATIALS_ALLOC)) {
		dict->spatials = realloc(dict->spatials,
				(dict->nspatials + SPATIALS_ALLOC)
				* sizeof(*dict->spatials));
	}
	dict->spatials[dict->nspatials] = *s;
	return dict->nspatials++;
}

static bool
spatial_dict_addh(struct spatial_dict *dict, hash_t hash, int id)
{
	if (dict->hash[hash]) {
		dict->collisions++;
		/* Give up, not worth the trouble. */
		return false;
	}
	dict->hash[hash] = id;
	return true;
}

/* Spatial dictionary file format:
 * /^#/ - comments
 * INDEX RADIUS STONES HASH...
 * INDEX: index in the spatial table
 * RADIUS: @d of the pattern
 * STONES: string of ".XO#" chars
 * HASH...: space-separated 18bit hash-table indices for the pattern */

static void
spatial_dict_read(struct spatial_dict *dict, char *buf)
{
	/* XXX: We trust the data. Bad data will crash us. */
	char *bufp = buf;

	int index, radius;
	index = strtol(bufp, &bufp, 10);
	radius = strtol(bufp, &bufp, 10);
	while (isspace(*bufp)) bufp++;

	/* Load the stone configuration. */
	struct spatial s = { .dist = radius };
	int sl = 0;
	while (!isspace(*bufp)) {
		s.points[sl / 4] |= char2stone(*bufp++) << (sl % 4);
		sl++;
	}
	while (isspace(*bufp)) bufp++;

	/* Sanity check. */
	if (sl != ptind[s.dist + 1]) {
		fprintf(stderr, "Spatial dictionary: Invalid number of stones (%d != %d) on this line: %s\n",
			sl, ptind[radius + 1] - 1, buf);
		exit(EXIT_FAILURE);
	}

	/* Add to collection. */
	int id = spatial_dict_addc(dict, &s);

	/* Add to specified hash places. */
	while (*bufp) {
		int hash = strtol(bufp, &bufp, 16);
		while (isspace(*bufp)) bufp++;
		spatial_dict_addh(dict, hash & spatial_hash_mask, id);
	}
}

static char *
spatial2str(struct spatial *s)
{
	static char buf[1024];
	for (int i = 0; i < ptind[s->dist + 1]; i++) {
		buf[i] = stone2char(spatial_point_at(*s, i));
	}
	buf[ptind[s->dist + 1]] = 0;
	return buf;
}

static void
spatial_dict_write(struct spatial_dict *dict, int id, FILE *f)
{
	struct spatial *s = &dict->spatials[id];
	fprintf(f, "%d %d ", id, s->dist);
	fputs(spatial2str(s), f);
	for (int r = 0; r < PTH__ROTATIONS; r++)
		fprintf(f, " %"PRIhash"", spatial_hash(r, s));
	fputc('\n', f);
}

static void
spatial_dict_load(struct spatial_dict *dict, FILE *f)
{
	char buf[1024];
	while (fgets(buf, sizeof(buf), f)) {
		if (buf[0] == '#') continue;
		spatial_dict_read(dict, buf);
	}
}

struct spatial_dict *
spatial_dict_init(bool will_append)
{
	static const char *filename = "patterns.spat";
	FILE *f = fopen(filename, "r");
	if (!f && !will_append) {
		if (DEBUGL(1))
			fprintf(stderr, "No spatial dictionary, will not match spatial pattern features.\n");
		return NULL;
	}

	struct spatial_dict *dict = calloc(1, sizeof(*dict));
	/* We create a dummy record for index 0 that we will
	 * never reference. This is so that hash value 0 can
	 * represent "no value". */
	struct spatial dummy = { .dist = 0 };
	spatial_dict_addc(dict, &dummy);

	if (f) {
		/* Existing file. Load it up! */
		spatial_dict_load(dict, f);
		fclose(f); f = NULL;
		if (will_append)
			f = fopen(filename, "a");
	} else {
		assert(will_append);
		f = fopen(filename, "a");
		/* New file. First, create a comment describing order
		 * of points in the array. This is just for purposes
		 * of external tools, Pachi never interprets it itself. */
		fprintf(f, "# Pachi spatial patterns dictionary v1.0 maxdist %d\n",
			MAX_PATTERN_DIST);
		for (int d = 0; d < MAX_PATTERN_DIST; d++) {
			fprintf(f, "# Point order: d=%d ", d);
			for (int j = ptind[d]; j < ptind[d + 1]; j++) {
				fprintf(f, "%d,%d ", ptcoords[j].x, ptcoords[j].y);
			}
			fprintf(f, "\n");
		}
	}

	dict->f = f;
	return dict;
}

void
spatial_from_board(struct pattern_config *pc, struct spatial *s,
                   struct board *b, struct move *m)
{
	assert(pc->spat_min > 0);

	enum stone (*bt)[4] = m->color == S_WHITE ? &bt_white : &bt_black;

	for (int j = 0; j < ptind[pc->spat_max]; j++) {
		/* Go through all points in given distance. */
		int x = coord_x(m->coord, b) + ptcoords[j].x;
		int y = coord_y(m->coord, b) + ptcoords[j].y;
		if (x >= board_size(b)) x = board_size(b) - 1; else if (x < 0) x = 0;
		if (y >= board_size(b)) y = board_size(b) - 1; else if (y < 0) y = 0;
		/* Append point. */
		s->points[j / 4] |= (*bt)[board_atxy(b, x, y)] << ((j % 4) * 2);
	}
	s->dist = ptind[pc->spat_max];
}

int
spatial_dict_get(struct spatial_dict *dict, struct spatial *s, hash_t hash)
{
	int id = dict->hash[hash];
	if (id && dict->spatials[id].dist != s->dist) {
		if (DEBUGL(6))
			fprintf(stderr, "Collision dist %d vs %d (hash [%d]%"PRIhash")\n",
				s->dist, dict->spatials[id].dist, id, hash);
		return 0;
	}
	return id;
}

int
spatial_dict_put(struct spatial_dict *dict, struct spatial *s, hash_t h)
{
	int id = spatial_dict_get(dict, s, h);
	if (id > 0) {
		/* Check for collisions in append mode. */
		/* Tough job, we simply try if any other rotation
		 * is also covered by the existing record. */
		int r; hash_t rhash; int rid;
		for (r = 1; r < PTH__ROTATIONS; r++) {
			rhash = spatial_hash(r, s);
			rid = dict->hash[rhash];
			if (rid != id)
				goto collision;
		}
		/* All rotations match, id is good to go! */
		return id;

collision:
		if (DEBUGL(1))
			fprintf(stderr, "Collision %d vs %d (hash %d:%"PRIhash")\n",
				id, dict->nspatials, r, h);
		id = 0;
		/* dict->collisions++; gets done by addh */
	}

	/* Add new pattern! */
	id = spatial_dict_addc(dict, s);
	for (int r = 0; r < PTH__ROTATIONS; r++)
		spatial_dict_addh(dict, spatial_hash(r, s), id);
	spatial_dict_write(dict, id, dict->f);
	return id;
}
