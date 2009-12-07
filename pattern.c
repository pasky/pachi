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
	p->f = NULL;
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
