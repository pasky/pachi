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
	if (cached_dict) {
		cached_dict->pc = pc;
		return cached_dict;
	}

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

	char *sphcachehit = malloc(pc->spat_dict->nspatials);
	hash_t (*sphcache)[PTH__ROTATIONS] = malloc(pc->spat_dict->nspatials * sizeof(sphcache[0]));

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

		/* We rehash spatials in the order of loaded patterns. This way
		 * we make sure that the most popular patterns will be hashed
		 * last and therefore take priority. */
		if (!sphcachehit[spi]) {
			sphcachehit[spi] = 1;
			for (int r = 0; r < PTH__ROTATIONS; r++)
				sphcache[spi][r] = spatial_hash(r, &pc->spat_dict->spatials[spi]);
		}
		for (int r = 0; r < PTH__ROTATIONS; r++)
			spatial_dict_addh(pc->spat_dict, sphcache[spi][r], spi);

		i++;
	}

	free(sphcache);
	free(sphcachehit);
	if (DEBUGL(3))
		spatial_dict_hashstats(pc->spat_dict);

	fclose(f);
	if (DEBUGL(1))
		fprintf(stderr, "Loaded %d pattern-probability pairs.\n", i);
	cached_dict = dict;
	return dict;
}

floating_t
pattern_rate_moves(struct pattern_config *pc, pattern_spec *ps, struct pattern_pdict *pd,
                   struct board *b, enum stone color,
                   struct pattern *pats, floating_t *probs)
{
	floating_t total = 0;
	for (int f = 0; f < b->flen; f++) {
		probs[f] = NAN;

		struct move mo = { .coord = b->f[f], .color = color };
		if (is_pass(mo.coord))
			continue;
		if (!board_is_valid_move(b, &mo))
			continue;

		pattern_match(pc, *ps, &pats[f], b, &mo);
		floating_t prob = pattern_prob(pd, &pats[f]);
		if (!isnan(prob)) {
			probs[f] = prob;
			total += prob;
		}
	}
	return total;
}

