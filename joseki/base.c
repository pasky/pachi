#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "move.h"
#include "joseki/base.h"


struct joseki_dict *
joseki_init(int bsize)
{
	struct joseki_dict *jd = calloc(1, sizeof(*jd));
	jd->bsize = bsize;
	jd->patterns = calloc(1 << joseki_hash_bits, sizeof(jd->patterns[0]));
	return jd;
}

struct joseki_dict *
joseki_load(int bsize)
{
	char fname[1024];
	snprintf(fname, 1024, "joseki%d.pdict", bsize - 2);
	FILE *f = fopen(fname, "r");
	if (!f) {
		if (DEBUGL(3))
			perror(fname);
		return NULL;
	}
	struct joseki_dict *jd = joseki_init(bsize);

	char linebuf[1024];
	while (fgets(linebuf, 1024, f)) {
		char *line = linebuf;

		while (isspace(*line)) line++;
		if (*line == '#')
			continue;
		hash_t h = strtoull(line, &line, 16);
		while (isspace(*line)) line++;
		enum stone color = *line++ == 'b' ? S_BLACK : S_WHITE;
		while (isspace(*line)) line++;

		/* Get count. */
		char *cs = strrchr(line, ' '); assert(cs);
		*cs++ = 0;
		int count = atoi(cs);
		
		coord_t **ccp = &jd->patterns[h].moves[color - 1];
		assert(!*ccp);
		*ccp = calloc2(count + 1, sizeof(coord_t));
		coord_t *cc = *ccp;
		while (*line) {
			assert(cc - *ccp < count);
			coord_t *c = str2coord(line, bsize);
			*cc++ = *c;
			coord_done(c);
			line += strcspn(line, " ");
			line += strspn(line, " ");
		}
		*cc = pass;
	}

	fclose(f);
	if (DEBUGL(2))
		fprintf(stderr, "Joseki dictionary for board size %d loaded.\n", bsize - 2);
	return jd;
}

void
joseki_done(struct joseki_dict *jd)
{
	if (!jd) return;
	free(jd->patterns);
	free(jd);
}
