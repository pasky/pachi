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
	.spat_min = 0, .spat_max = 0, /* Unsupported. */
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


void
pattern_get(struct pattern_config *pc, struct pattern *p, struct board *b, struct move *m)
{
	p->n = 0;
	struct feature *f = &p->f[0];

	/* TODO: We should match pretty much all of these features
	 * incrementally. */

	/* FEAT_SPATIAL */
	/* TODO */
	assert(!pc->spat_max);

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
