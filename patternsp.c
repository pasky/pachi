#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "patternsp.h"

/* Mapping from point sequence to coordinate offsets (to determine
 * coordinates relative to pattern center). The array is ordered
 * in the gridcular metric order so that we can go through it
 * and incrementally match spatial features in nested circles.
 * Within one circle, coordinates are ordered by rows to keep
 * good cache behavior. */
struct ptcoord ptcoords[MAX_PATTERN_AREA];

/* For each radius, starting index in ptcoords[]. */
unsigned int ptind[MAX_PATTERN_DIST + 2];

/* ptcoords[], ptind[] setup */
static void
ptcoords_init(void)
{
	int i = 0; /* Indexing ptcoords[] */

	/* First, center point. */
	ptind[0] = ptind[1] = 0;
	ptcoords[i].x = ptcoords[i].y = 0; i++;

	for (int d = 2; d <= MAX_PATTERN_DIST; d++) {
		ptind[d] = i;
		/* For each y, examine all integer solutions
		 * of d = |x| + |y| + max(|x|, |y|). */
		/* TODO: (Stern, 2006) uses a hand-modified
		 * circles that are finer for small d and more
		 * coarse for large d. */
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
	ptind[MAX_PATTERN_DIST + 1] = i;

#if 0
	for (int d = 0; d <= MAX_PATTERN_DIST; d++) {
		fprintf(stderr, "d=%d (%d) ", d, ptind[d]);
		for (int j = ptind[d]; j < ptind[d + 1]; j++) {
			fprintf(stderr, "%d,%d ", ptcoords[j].x, ptcoords[j].y);
		}
		fprintf(stderr, "\n");
	}
#endif
}


/* Zobrist hashes used for points in patterns. */
hash_t pthashes[PTH__ROTATIONS][MAX_PATTERN_AREA][S_MAX];

static void
pthashes_init(void)
{
	/* We need fixed hashes for all pattern-relative in
	 * all pattern users! This is a simple way to generate
	 * hopefully good ones. Park-Miller powa. :) */

	/* We create a virtual board (centered at the sequence start),
	 * plant the hashes there, then pick them up into the sequence
	 * with correct coordinates. It would be possible to generate
	 * the sequence point hashes directly, but the rotations would
	 * make for enormous headaches. */
#define PATTERN_BOARD_SIZE ((MAX_PATTERN_DIST + 1) * (MAX_PATTERN_DIST + 1))
	hash_t pthboard[PATTERN_BOARD_SIZE][4];
	int pthbc = PATTERN_BOARD_SIZE / 2; // tengen coord

	/* The magic numbers are tuned for minimal collisions. */
	hash_t h1 = 0xd6d6d6d1;
	hash_t h2 = 0xd6d6d6d2;
	hash_t h3 = 0xd6d6d6d3;
	hash_t h4 = 0xd6d6d6d4;
	for (int i = 0; i < PATTERN_BOARD_SIZE; i++) {
		pthboard[i][S_NONE] = (h1 = h1 * 16787);
		pthboard[i][S_BLACK] = (h2 = h2 * 16823);
		pthboard[i][S_WHITE] = (h3 = h3 * 16811 - 13);
		pthboard[i][S_OFFBOARD] = (h4 = h4 * 16811);
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
			int bi = pthbc + ry * (MAX_PATTERN_DIST + 1) + rx;

			/* Copy info. */
			pthashes[r][i][S_NONE] = pthboard[bi][S_NONE];
			pthashes[r][i][S_BLACK] = pthboard[bi][S_BLACK];
			pthashes[r][i][S_WHITE] = pthboard[bi][S_WHITE];
			pthashes[r][i][S_OFFBOARD] = pthboard[bi][S_OFFBOARD];
		}
	}
}

static void __attribute__((constructor))
spatial_init(void)
{
	/* Initialization of various static data structures for
	 * fast pattern processing. */
	ptcoords_init();
	pthashes_init();
}

inline hash_t
spatial_hash(unsigned int rotation, struct spatial *s)
{
	hash_t h = 0;
	for (unsigned int i = 0; i < ptind[s->dist + 1]; i++) {
		h ^= pthashes[rotation][i][spatial_point_at(*s, i)];
	}
	return h & spatial_hash_mask;
}

char *
spatial2str(struct spatial *s)
{
	static char buf[1024];
	for (unsigned int i = 0; i < ptind[s->dist + 1]; i++) {
		buf[i] = stone2char(spatial_point_at(*s, i));
	}
	buf[ptind[s->dist + 1]] = 0;
	return buf;
}

void
spatial_from_board(struct pattern_config *pc, struct spatial *s,
                   struct board *b, struct move *m)
{
	assert(pc->spat_min > 0);

	/* We record all spatial patterns black-to-play; simply
	 * reverse all colors if we are white-to-play. */
	static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
	static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	enum stone (*bt)[4] = m->color == S_WHITE ? &bt_white : &bt_black;

	memset(s, 0, sizeof(*s));
	for (unsigned int j = 0; j < ptind[pc->spat_max + 1]; j++) {
		ptcoords_at(x, y, m->coord, b, j);
		s->points[j / 4] |= (*bt)[board_atxy(b, x, y)] << ((j % 4) * 2);
	}
	s->dist = pc->spat_max;
}

/* Compare two spatials, allowing for differences up to isomorphism.
 * True means the spatials are equivalent. */
static bool
spatial_cmp(struct spatial *s1, struct spatial *s2)
{
	/* Quick preliminary check. */
	if (s1->dist != s2->dist)
		return false;

	/* We could create complex transposition tables, but it seems most
	 * foolproof to just check if the sets of rotation hashes are the
	 * same for both. */
	hash_t s1r[PTH__ROTATIONS];
	for (unsigned int r = 0; r < PTH__ROTATIONS; r++)
		s1r[r] = spatial_hash(r, s1);
	for (unsigned int r = 0; r < PTH__ROTATIONS; r++) {
		hash_t s2r = spatial_hash(r, s2);
		for (unsigned int p = 0; p < PTH__ROTATIONS; p++)
			if (s2r == s1r[p])
				goto found_rot;
		/* Rotation hash s2r does not correspond to s1r. */
		return false;
found_rot:;
	}

	/* All rotation hashes of s2 occur in s1. Hopefully that
	 * indicates something. */
	return true;
}


/* Spatial dict manipulation. */

static unsigned int
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

bool
spatial_dict_addh(struct spatial_dict *dict, hash_t hash, unsigned int id)
{
	if (dict->hash[hash]) {
		if (dict->hash[hash] != id)
			dict->collisions++;
	} else {
		dict->fills++;
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
spatial_dict_read(struct spatial_dict *dict, char *buf, bool hash)
{
	/* XXX: We trust the data. Bad data will crash us. */
	char *bufp = buf;

	unsigned int index, radius;
	index = strtoul(bufp, &bufp, 10);
	radius = strtoul(bufp, &bufp, 10);
	while (isspace(*bufp)) bufp++;

	if (radius > MAX_PATTERN_DIST) {
		/* Too large spatial, skip. */
		struct spatial s = { .dist = 0 };
		unsigned int id = spatial_dict_addc(dict, &s);
		assert(id == index);
		return;
	}

	/* Load the stone configuration. */
	struct spatial s = { .dist = radius };
	unsigned int sl = 0;
	while (!isspace(*bufp)) {
		s.points[sl / 4] |= char2stone(*bufp++) << ((sl % 4)*2);
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
	unsigned int id = spatial_dict_addc(dict, &s);
	assert(id == index);

	/* Add to specified hash places. */
	if (hash)
		for (unsigned int r = 0; r < PTH__ROTATIONS; r++)
			spatial_dict_addh(dict, spatial_hash(r, &s), id);
}

void
spatial_write(struct spatial_dict *dict, struct spatial *s, unsigned int id, FILE *f)
{
	fprintf(f, "%d %d ", id, s->dist);
	fputs(spatial2str(s), f);
	for (unsigned int r = 0; r < PTH__ROTATIONS; r++) {
		hash_t rhash = spatial_hash(r, s);
		unsigned int id2 = dict->hash[rhash];
		if (id2 != id) {
			/* This hash does not belong to us. Decide whether
			 * we or the current owner is better owner. */
			/* TODO: Compare also # of patternscan encounters? */
			struct spatial *s2 = &dict->spatials[id2];
			if (s2->dist < s->dist)
				continue;
			if (s2->dist == s->dist && id2 < id)
				continue;
		}
		fprintf(f, " %"PRIhash"", spatial_hash(r, s));
	}
	fputc('\n', f);
}

static void
spatial_dict_load(struct spatial_dict *dict, FILE *f, bool hash)
{
	char buf[1024];
	while (fgets(buf, sizeof(buf), f)) {
		if (buf[0] == '#') continue;
		spatial_dict_read(dict, buf, hash);
	}
	if (DEBUGL(1)) {
		fprintf(stderr, "Loaded spatial dictionary of %d patterns.\n", dict->nspatials);
		if (hash)
			spatial_dict_hashstats(dict);
	}
}

void
spatial_dict_hashstats(struct spatial_dict *dict)
{
	/* m hash size, n number of patterns; is zobrist universal hash?
	 *
	 * Not so rigorous analysis, but it should give a good approximation:
	 * Probability of empty bucket is (1-1/m)^n ~ e^(-n/m)
	 * Probability of non-empty bucket is 1-e^(-n/m)
	 * Expected number of non-empty buckets is m*(1-e^(-n/m))
	 * Number of collisions is n-m*(1-e^(-n/m)). */

	/* The result: Reality matches these expectations pretty well!
	 *
	 * Actual:
	 * 	Loaded spatial dictionary of 1064482 patterns.
	 * 	(Spatial dictionary hash: 513997 collisions (incl. repetitions), 11.88% (7970033/67108864) fill rate).
	 *
	 * Theoretical:
	 * 	m = 2^26
	 * 	n <= 8*1064482 (some patterns may have some identical rotations)
	 * 	n = 513997+7970033 = 8484030 should be the correct number
	 * 	n-m*(1-e^(-n/m)) = 514381
	 *
	 * To verify, make sure to turn patternprob off (e.g. use
	 * -e patternscan), since it will insert a pattern multiple times,
	 * multiplying the reported number of collisions. */

	unsigned long buckets = (sizeof(dict->hash) / sizeof(dict->hash[0]));
	fprintf(stderr, "\t(Spatial dictionary hash: %d collisions (incl. repetitions), %.2f%% (%d/%lu) fill rate).\n",
			dict->collisions,
			(double) dict->fills * 100 / buckets,
			dict->fills, buckets);
}

void
spatial_dict_writeinfo(struct spatial_dict *dict, FILE *f)
{
	/* New file. First, create a comment describing order
	 * of points in the array. This is just for purposes
	 * of external tools, Pachi never interprets it itself. */
	fprintf(f, "# Pachi spatial patterns dictionary v1.0 maxdist %d\n",
		MAX_PATTERN_DIST);
	for (unsigned int d = 0; d <= MAX_PATTERN_DIST; d++) {
		fprintf(f, "# Point order: d=%d ", d);
		for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			fprintf(f, "%d,%d ", ptcoords[j].x, ptcoords[j].y);
		}
		fprintf(f, "\n");
	}
}

/* We try to avoid needlessly reloading spatial dictionary
 * since it may take rather long time. */
static struct spatial_dict *cached_dict;

const char *spatial_dict_filename = "patterns.spat";
struct spatial_dict *
spatial_dict_init(bool will_append, bool hash)
{
	if (cached_dict && !will_append)
		return cached_dict;

	FILE *f = fopen(spatial_dict_filename, "r");
	if (!f && !will_append) {
		if (DEBUGL(1))
			fprintf(stderr, "No spatial dictionary, will not match spatial pattern features.\n");
		return NULL;
	}

	struct spatial_dict *dict = calloc2(1, sizeof(*dict));
	/* We create a dummy record for index 0 that we will
	 * never reference. This is so that hash value 0 can
	 * represent "no value". */
	struct spatial dummy = { .dist = 0 };
	spatial_dict_addc(dict, &dummy);

	if (f) {
		spatial_dict_load(dict, f, hash);
		fclose(f); f = NULL;
	} else {
		assert(will_append);
	}

	cached_dict = dict;
	return dict;
}

unsigned int
spatial_dict_put(struct spatial_dict *dict, struct spatial *s, hash_t h)
{
	/* We avoid spatial_dict_get() here, since we want to ignore radius
	 * differences - we have custom collision detection. */
	unsigned int id = dict->hash[h];
	if (id > 0) {
		/* Is this the same or isomorphous spatial? */
		if (spatial_cmp(s, &dict->spatials[id]))
			return id;

		/* Look a bit harder - perhaps one of our rotations still
		 * points at the correct spatial. */
		for (unsigned int r = 0; r < PTH__ROTATIONS; r++) {
			hash_t rhash = spatial_hash(r, s);
			unsigned int rid = dict->hash[rhash];
			/* No match means we definitely aren't stored yet. */
			if (!rid)
				break;
			if (id != rid && spatial_cmp(s, &dict->spatials[rid])) {
				/* Yay, this is us! */
				if (DEBUGL(3))
					fprintf(stderr, "Repeated collision %d vs %d\n", id, rid);
				id = rid;
				/* Point the hashes back to us. */
				goto hash_store;
			}
		}

		if (DEBUGL(1))
			fprintf(stderr, "Collision %d vs %d\n", id, dict->nspatials);
		id = 0;
		/* dict->collisions++; gets done by addh */
	}

	/* Add new pattern! */
	id = spatial_dict_addc(dict, s);
	if (DEBUGL(4)) {
		fprintf(stderr, "new spat %d(%d) %s <%"PRIhash"> ", id, s->dist, spatial2str(s), h);
		for (unsigned int r = 0; r < 8; r++)
			fprintf(stderr,"[%"PRIhash"] ", spatial_hash(r, s));
		fprintf(stderr, "\n");
	}

	/* Store new pattern in the hash. */
hash_store:
	for (unsigned int r = 0; r < PTH__ROTATIONS; r++)
		spatial_dict_addh(dict, spatial_hash(r, s), id);

	return id;
}
