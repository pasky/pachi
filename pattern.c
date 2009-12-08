#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "tactics.h"


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
pattern_get(struct pattern *p, struct board *b, struct move *m)
{
	p->n = 0;
	struct feature *f = &p->f[0];

	/* TODO: We should match pretty much all of these features
	 * incrementally. */

	/* FEAT_SPATIAL */
	/* TODO */

	/* FEAT_PASS */
	if (is_pass(m->coord)) {
		f->id = FEAT_PASS;
		f->payload |= (b->moves > 0 && is_pass(b->last_move.coord)) << PF_PASS_LASTPASS;
		f++, p->n++;
	}

	/* FEAT_CAPTURE */
	{
		foreach_neighbor(b, m->coord, {
			group_t g = group_at(b, c);
			if (!g || board_group_info(b, g).libs != 1)
				continue;

			/* Capture! */
			f->id = FEAT_CAPTURE;
			f->payload = 0;

			/* TODO: Ko capture flag */

			f->payload |= is_ladder(b, m->coord, g, true, true) << PF_CAPTURE_LADDER;

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

	/* FEAT_SELFATARI */

	/* FEAT_ATARI */

	/* FEAT_BORDER */

	/* FEAT_LDIST */

	/* FEAT_LLDIST */

	/* FEAT_MCOWNER */
}

char *
pattern2str(char *str, struct pattern *p)
{
	for (int i = 0; i < p->n; i++) {
		if (i > 0) strcat(str++, " ");
		str = feature2str(str, &p->f[i]);
	}
	return str;
}
