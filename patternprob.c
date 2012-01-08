#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "patternsp.h"
#include "patternprob.h"


/* We try to avoid needlessly reloading probability dictionary
 * since it may take rather long time. */
static struct pattern_pdict *cached_dict;

struct pattern_pdict *
pattern_pdict_init(char *filename, struct pattern_config *pc)
{
	if (cached_dict)
		return cached_dict;

	if (!filename)
		filename = "patterns.prob";
	FILE *f = fopen(filename, "r");
	if (!f) {
		if (DEBUGL(1))
			fprintf(stderr, "No pattern probtable, will not use learned patterns.\n");
		return NULL;
	}

	struct pattern_pdict *dict = calloc2(1, sizeof(*dict));
	dict->pc = pc;
	dict->table = calloc2(pc->spat_dict->nspatials + 1, sizeof(*dict->table));

	int i = 0;
	char sbuf[1024];
	while (fgets(sbuf, sizeof(sbuf), f)) {
		struct pattern_prob *pb = calloc2(1, sizeof(*pb));
		int c, o;

		char *buf = sbuf;
		if (buf[0] == '#') continue;
		while (isspace(*buf)) buf++;
		while (!isspace(*buf)) buf++; // we recompute the probability
		while (isspace(*buf)) buf++;
		c = strtol(buf, &buf, 10);
		while (isspace(*buf)) buf++;
		o = strtol(buf, &buf, 10);
		pb->prob = (floating_t) c / o;
		while (isspace(*buf)) buf++;
		str2pattern(buf, &pb->p);

		uint32_t spi = pattern2spatial(dict, &pb->p);
		pb->next = dict->table[spi];
		dict->table[spi] = pb;
		i++;
	}

	fclose(f);
	if (DEBUGL(1))
		fprintf(stderr, "Loaded %d pattern-probability pairs.\n", i);
	cached_dict = dict;
	return dict;
}
