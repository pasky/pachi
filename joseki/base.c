#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "joseki/base.h"


struct joseki joseki_pats[1 << joseki_hash_bits];

void
joseki_load(void)
{
	FILE *f = fopen("pachijoseki.dat", "r");
	if (!f) return;

	char linebuf[1024];
	while (fgets(linebuf, 1024, f)) {
		char *line = linebuf;

		hash_t h = strtoull(line, &line, 16);
		while (isspace(*line)) line++;
		enum stone color = *line++ == 'b' ? S_BLACK : S_WHITE;
		while (isspace(*line)) line++;

		/* Get count. */
		char *cs = strrchr(line, ' '); assert(cs);
		*cs++ = 0;
		int count = atoi(cs);
		
		coord_t **ccp = &joseki_pats[h].moves[color - 1];
		assert(!*ccp);
		*ccp = calloc2(count + 1, sizeof(coord_t));
		coord_t *cc = *ccp;
		while (*line) {
			assert(cc - *ccp < count);
			coord_t *c = str2coord(line, 21 /* XXX */);
			*cc++ = *c;
			coord_done(c);
			line += strcspn(line, " ");
			line += strspn(line, " ");
		}
		*cc = pass;
	}

	fclose(f);
}
