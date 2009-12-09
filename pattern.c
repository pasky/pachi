#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "tactics.h"


/* Maximum spatial pattern diameter. */
#define MAX_PATTERN_DIST 21

struct pattern_config DEFAULT_PATTERN_CONFIG = {
	.spat_min = 2, .spat_max = MAX_PATTERN_DIST, /* Unsupported. */
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
	return str + sprintf(str + strlen(str), "%s:%"PRIx64, fnames[f->id], f->payload);
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


/* Maximum number of points in spatial pattern (upper bound). */
#define MAX_PATTERN_AREA (MAX_PATTERN_DIST*MAX_PATTERN_DIST)

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
			if (y * 2 > d / 2) {
				/* max(|x|, |y|) = |y|, non-zero x */
				x = d - y * 2;
				if (x + y * 2 != d) continue;
			} else {
				/* max(|x|, |y|) = |x| */
				/* Or, max(|x|, |y|) = |y| and x is zero */
				x = (d - y) / 2;
				if (x * 2 + y != d) continue;
			}

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

/* Zobrist hashes used for black/white stones in patterns. */
static hash_t pthashes[MAX_PATTERN_AREA][4];
static void __attribute__((constructor)) pthashes_init(void)
{
	/* We need fixed hashes for all pattern-relative in
	 * all pattern users! This is a simple way to generate
	 * hopefully good ones. Park-Miller powa. :) */
	hash_t h = 31;
	for (int i = 0; i < MAX_PATTERN_AREA; i++) {
		pthashes[i][S_NONE] = 0;
		pthashes[i][S_BLACK] = (h *= 16807);
		pthashes[i][S_WHITE] = (h *= 16807);
		pthashes[i][S_OFFBOARD] = (h *= 16807);
	}
}


void
pattern_get(struct pattern_config *pc, struct pattern *p, struct board *b, struct move *m)
{
	p->n = 0;
	struct feature *f = &p->f[0];

	/* TODO: We should match pretty much all of these features
	 * incrementally. */

	/* FEAT_SPATIAL */
	if (pc->spat_max > 0) {
		assert(pc->spat_min > 0);
		hash_t h = 0;
		for (int d = pc->spat_min; d < pc->spat_max; d++) {
			for (int j = ptind[d]; j < ptind[d + 1]; j++) {
				int x = coord_x(m->coord, b) + ptcoords[j].x;
				int y = coord_y(m->coord, b) + ptcoords[j].y;
				if (x >= board_size(b)) x = board_size(b) - 1; else if (x < 0) x = 0;
				if (y >= board_size(b)) y = board_size(b) - 1; else if (y < 0) y = 0;
				h ^= pthashes[j][board_atxy(b, x, y)];
			}
			f->id = FEAT_SPATIAL;
			f->payload = h & ((1ULL << 56) - 1);
			f->payload |= (uint64_t)d << 56;
			(f++, p->n++);
		}
	}

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
