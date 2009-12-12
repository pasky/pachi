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
	.spat_min = 2, .spat_max = MAX_PATTERN_DIST,
	.bdist_max = 4,
	.ldist_min = 0, .ldist_max = 256,
	.mcsims = 0, /* Unsupported. */
};


static const char *fnames[] = {
	[FEAT_SPATIAL] = "s",
	[FEAT_PASS] = "pass",
	[FEAT_CAPTURE] = "capture",
	[FEAT_AESCAPE] = "atariescape",
	[FEAT_SELFATARI] = "selfatari",
	[FEAT_ATARI] = "atari",
	[FEAT_BORDER] = "border",
	[FEAT_LDIST] = "ldist",
	[FEAT_LLDIST] = "lldist",
	[FEAT_MCOWNER] = "mcowner",
};

char *
feature2str(char *str, struct feature *f)
{
	return str + sprintf(str + strlen(str), "%s:%"PRIx32, fnames[f->id], f->payload);
}

char *
str2feature(char *str, struct feature *f)
{
	while (isspace(*str)) str++;

	int flen = strcspn(str, ":");
	for (int i = 0; i < sizeof(fnames)/sizeof(fnames[0]); i++)
		if (strlen(fnames[i]) == flen && strncmp(fnames[i], str, flen)) {
			f->id = i;
			goto found;
		}
	fprintf(stderr, "invalid featurespec: %s\n", str);
	exit(EXIT_FAILURE);

found:
	str += flen + 1;
	f->payload = strtoull(str, &str, 10);
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


void
pattern_match(struct pattern_config *pc, struct pattern *p, struct board *b, struct move *m)
{
	p->n = 0;
	struct feature *f = &p->f[0];

	/* TODO: We should match pretty much all of these features
	 * incrementally. */

	/* FEAT_PASS */
	if (is_pass(m->coord)) {
		f->id = FEAT_PASS; f->payload = 0;
		f->payload |= (b->moves > 0 && is_pass(b->last_move.coord)) << PF_PASS_LASTPASS;
		p->n++;
		return;
	}

	/* FEAT_CAPTURE */
	{
		foreach_neighbor(b, m->coord, {
			if (board_at(b, c) != stone_other(m->color))
				continue;
			group_t g = group_at(b, c);
			if (!g || board_group_info(b, g).libs != 1)
				continue;

			/* Capture! */
			f->id = FEAT_CAPTURE; f->payload = 0;

			f->payload |= is_ladder(b, m->coord, g, true, true) << PF_CAPTURE_LADDER;
			/* TODO: is_ladder() is too conservative in some
			 * very obvious situations, look at complete.gtp. */

			/* TODO: PF_CAPTURE_RECAPTURE */

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

			if (group_is_onestone(b, g)
			    && neighbor_count_at(b, m->coord, stone_other(m->color))
			       + neighbor_count_at(b, m->coord, S_OFFBOARD) == 4)
				f->payload |= 1 << PF_CAPTURE_KO;

			(f++, p->n++);
		});
	}


	/* FEAT_AESCAPE */
	{
		foreach_neighbor(b, m->coord, {
			if (board_at(b, c) != m->color)
				continue;
			group_t g = group_at(b, c);
			if (!g || board_group_info(b, g).libs != 1)
				continue;

			/* In atari! */
			f->id = FEAT_AESCAPE; f->payload = 0;

			f->payload |= is_ladder(b, m->coord, g, true, true) << PF_AESCAPE_LADDER;
			/* TODO: is_ladder() is too conservative in some
			 * very obvious situations, look at complete.gtp. */

			(f++, p->n++);
		});
	}


	/* FEAT_SELFATARI */
	if (is_bad_selfatari(b, m->color, m->coord)) {
		f->id = FEAT_SELFATARI;
		/* TODO: Dumb selfatari detection. */
		f->payload = 1 << PF_SELFATARI_SMART;
	}

	/* FEAT_ATARI */
	{
		foreach_neighbor(b, m->coord, {
			if (board_at(b, c) != stone_other(m->color))
				continue;
			group_t g = group_at(b, c);
			if (!g || board_group_info(b, g).libs != 2)
				continue;

			/* Can atari! */
			f->id = FEAT_ATARI; f->payload = 0;

			f->payload |= is_ladder(b, m->coord, g, true, true) << PF_ATARI_LADDER;
			/* TODO: is_ladder() is too conservative in some
			 * very obvious situations, look at complete.gtp. */

			if (!is_pass(b->ko.coord))
				f->payload |= 1 << PF_CAPTURE_KO;

			(f++, p->n++);
		});
	}

	/* FEAT_BORDER */
	int bdist = coord_edge_distance(m->coord, b);
	if (bdist <= pc->bdist_max) {
		f->id = FEAT_BORDER;
		f->payload = bdist;
		(f++, p->n++);
	}

	/* FEAT_LDIST */
	if (pc->ldist_max > 0 && !is_pass(b->last_move.coord)) {
		int ldist = coord_gridcular_distance(m->coord, b->last_move.coord, b);
		if (pc->ldist_min <= ldist && ldist <= pc->ldist_max) {
			f->id = FEAT_LDIST;
			f->payload = ldist;
			(f++, p->n++);
		}
	}

	/* FEAT_LLDIST */
	if (pc->ldist_max > 0 && !is_pass(b->last_move.coord)) {
		int lldist = coord_gridcular_distance(m->coord, b->last_move2.coord, b);
		if (pc->ldist_min <= lldist && lldist <= pc->ldist_max) {
			f->id = FEAT_LLDIST;
			f->payload = lldist;
			(f++, p->n++);
		}
	}

	/* FEAT_SPATIAL */
	if (pc->spat_max > 0 && pc->spat_dict) {
		assert(pc->spat_min > 0);

		struct spatial s = { .points = {0} };
		for (int d = 2; d < pc->spat_max; d++) {
			/* Go through all points in given distance. */
			for (int j = ptind[d]; j < ptind[d + 1]; j++) {
				int x = coord_x(m->coord, b) + ptcoords[j].x;
				int y = coord_y(m->coord, b) + ptcoords[j].y;
				if (x >= board_size(b)) x = board_size(b) - 1; else if (x < 0) x = 0;
				if (y >= board_size(b)) y = board_size(b) - 1; else if (y < 0) y = 0;
				/* Append point. */
				s.points[j / 4] |= board_atxy(b, x, y) << ((j % 4) * 2);
			}
			if (d < pc->spat_min)
				continue;
			/* Record spatial feature, one per distance. */
			f->id = FEAT_SPATIAL;
			f->payload = (d << 24) | ((m->color == S_WHITE) << 23);
			s.dist = d;
			f->payload |= spatial_dict_get(pc->spat_dict, &s);
			(f++, p->n++);
		}
	}

	/* FEAT_MCOWNER */
	/* TODO */
	assert(!pc->mcsims);
}

char *
pattern2str(char *str, struct pattern *p)
{
	strcat(str++, "(");
	for (int i = 0; i < p->n; i++) {
		if (i > 0) strcat(str++, " ");
		str = feature2str(str, &p->f[i]);
	}
	strcat(str++, ")");
	return str;
}



/*** Spatial patterns dictionary */

/* Zobrist hashes used for black/white stones in patterns. */
#define PTH__ROTATIONS	16
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

	hash_t h = 31;
	for (int i = 0; i < MAX_PATTERN_AREA; i++) {
		pthboard[i][S_NONE] = (h *= 16807);
		pthboard[i][S_BLACK] = (h *= 16807);
		pthboard[i][S_WHITE] = (h *= 16807);
		pthboard[i][S_OFFBOARD] = (h *= 16807);
	}

	/* Virtual board with hashes created, now fill
	 * pthashes[] with hashes for points in actual
	 * sequences, also considering various rotations. */
#define PTH_VMIRROR	1
#define PTH_HMIRROR	2
#define PTH_90ROT	4
#define PTH_REVCOLOR	8
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
			if (r & PTH_REVCOLOR) {
				pthashes[r][i][S_WHITE] = pthboard[bi][S_BLACK];
				pthashes[r][i][S_BLACK] = pthboard[bi][S_WHITE];
			} else {
				pthashes[r][i][S_BLACK] = pthboard[bi][S_BLACK];
				pthashes[r][i][S_WHITE] = pthboard[bi][S_WHITE];
			}
			pthashes[r][i][S_OFFBOARD] = pthboard[bi][S_OFFBOARD];
		}
	}
}

static hash_t
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
	static const char *filename = "spatial.dict";
	FILE *f = fopen(filename, "r");
	if (!f && !will_append) {
		if (DEBUGL(2))
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

int
spatial_dict_get(struct spatial_dict *dict, struct spatial *s)
{
	hash_t hash = spatial_hash(0, s);
	int id = dict->hash[hash];
	if (id && dict->f) {
		/* Check for collisions in append mode. */
		/* Tough job, we simply try if all rotations
		 * are also covered by the existing record. */
		for (int r = 0; r < PTH__ROTATIONS; r++) {
			hash_t rhash = spatial_hash(r, s);
			int rid = dict->hash[rhash];
			if (rid == id)
				continue;
			if (DEBUGL(2))
				fprintf(stderr, "Collision %d vs %d (hash %d:%"PRIhash")\n",
					rid, dict->nspatials, r, rhash);
			id = 0;
			/* dict->collisions++; gets done by addh */
			break;
		}
	}
	if (id) return id;
	if (!dict->f) return -1;

	/* Add new pattern! */
	id = spatial_dict_addc(dict, s);
	for (int r = 0; r < PTH__ROTATIONS; r++)
		spatial_dict_addh(dict, spatial_hash(r, s), id);
	spatial_dict_write(dict, id, dict->f);
	return id;
}
