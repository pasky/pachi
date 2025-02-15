#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern/pattern.h"
#include "pattern/spatial.h"

/* Mapping from point sequence to coordinate offsets (to determine
 * coordinates relative to pattern center). The array is ordered
 * in the gridcular metric order so that we can go through it
 * and incrementally match spatial features in nested circles.
 * Within one circle, coordinates are ordered by rows to keep
 * good cache behavior. */
ptcoord_t ptcoords[MAX_PATTERN_AREA];

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

static void spatial_dict_hashstats(spatial_dict_t *dict);

static void __attribute__((constructor))
spatial_init(void)
{
	/* Initialization of various static data structures for
	 * fast pattern processing. */
	ptcoords_init();
	pthashes_init();
}

hash_t
spatial_hash(unsigned int rotation, spatial_t *s)
{
	hash_t h = 0;
	for (unsigned int i = 0; i < ptind[s->dist + 1]; i++) {
		h ^= pthashes[rotation][i][spatial_point(s, i)];
	}
	return h;
}

/* Compute spatial hash from board. */
hash_t
spatial_hash_from_board_rot(board_t *b, coord_t coord, enum stone color, int rot, unsigned int d)
{
	assert(d <= MAX_PATTERN_DIST);

	if (is_pass(coord) || is_resign(coord))  return 0;
	
	/* We record all spatial patterns black-to-play; simply
	 * reverse all colors if we are white-to-play. */
	static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
	static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	enum stone (*bt)[4] = (color == S_WHITE ? &bt_white : &bt_black);

	int cx = coord_x(coord), cy = coord_y(coord);
	hash_t h = 0;
	for (unsigned int i = 0; i < ptind[d + 1]; i++) {
		ptcoords_at(x, y, cx, cy, i);
		h ^= pthashes[rot][i][(*bt)[board_atxy(b, x, y)]];
	}
	return h;
}

hash_t
spatial_hash_from_board(board_t *b, coord_t coord, enum stone color, unsigned int d)
{
	return spatial_hash_from_board_rot(b, coord, color, 0, d);
}

/* S_MAX allowed here (= point not matched) */
static char
spatial_stone2char(enum stone s)
{
	return ".XO# "[s];
}

/* S_MAX allowed here (= point not matched) */
static enum stone
spatial_char2stone(char s)
{
	switch (s) {
		case '.': return S_NONE;
		case 'X': return S_BLACK;
		case 'O': return S_WHITE;
		case '#': return S_OFFBOARD;
		case ' ': return S_MAX;
	}
	assert(0);
}

char *
spatial2str(spatial_t *s)
{
	static char buf[1024];
	for (unsigned int i = 0; i < ptind[s->dist + 1]; i++)
		buf[i] = spatial_stone2char(spatial_point(s, i));
	buf[ptind[s->dist + 1]] = 0;
	return buf;
}

static char*
print_handler(board_t *board, coord_t c, void *data)
{
	static char buf[2];
	sprintf(buf, "%c", spatial_stone2char(board_at(board, c)));
	return buf;
}

void
spatial_print(board_t *board, FILE *f, spatial_t *s, coord_t c)
{
	int size = board_rsize(board);
	board_t *b = board_new(size, NULL);
	last_move(b).coord = c;

	/* Blank whole board so pattern stands out (only pattern area gets printed).
	 * Set every stone to S_MAX (spatial_stone2char(S_MAX) = ' ') */
	for (int i = 0; i < size; i++)
		for (int j = 0; j < size; j++) {
			coord_t c = coord_xy(i+1, j+1);
			board_at(b, c) = S_MAX;  // HACK
		}
	
	int cx = coord_x(c), cy = coord_y(c);
	for (unsigned int j = 0; j < ptind[s->dist + 1]; j++) {
		ptcoords_at(x, y, cx, cy, j);
		board_at(b, coord_xy(x, y)) = spatial_point(s, j);
	}
	board_hprint(b, stderr, print_handler, NULL);
	board_delete(&b);
}


void
spatial_from_board(pattern_config_t *pc, spatial_t *s,
                   board_t *b, move_t *m)
{
	assert(pc->spat_min > 0);

	/* We record all spatial patterns black-to-play; simply
	 * reverse all colors if we are white-to-play. */
	static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
	static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	enum stone (*bt)[4] = m->color == S_WHITE ? &bt_white : &bt_black;

	memset(s, 0, sizeof(*s));
	int cx = coord_x(m->coord), cy = coord_y(m->coord);
	for (unsigned int j = 0; j < ptind[pc->spat_max + 1]; j++) {
		ptcoords_at(x, y, cx, cy, j);
		enum stone color = (*bt)[board_atxy(b, x, y)];
		set_spatial_point(s, j, color);
	}
	s->dist = pc->spat_max;
}

/* Compare two spatials, allowing for differences up to isomorphism.
 * True means the spatials are equivalent. */
static bool
spatial_equal(spatial_t *s1, spatial_t *s2)
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


/**********************************************************************************/
/* Spatial dict manipulation. */

spatial_dict_t *spat_dict = NULL;

/* Spatial dict hashtable hash function. @h: spatial hash */
static unsigned int
spatial_dict_hash(hash_t h) {  return h & spatial_hash_mask;  }

spatial_t*
spatial_dict_lookup(int dist, hash_t hash)
{
	spatial_entry_t *e = spat_dict->hashtable[spatial_dict_hash(hash)];
	for (; e ; e = e->next)
		if (e->hash == hash && get_spatial(e->id)->dist == dist)
			return get_spatial(e->id);
	return NULL;
}

#ifndef GENSPATIAL	
#define SPATIALS_ALLOC 1024		/* Allocate space in 1024 blocks. */
#else	
#define SPATIALS_ALLOC (1024 * 1024)	/* Allocate space in 1M blocks. */
#endif

/* Add to collection, returns new pattern id */
static unsigned int
spatial_dict_addc(spatial_t *s)
{
	spatial_dict_t *d = spat_dict;
	
	if (!(d->nspatials % SPATIALS_ALLOC))
		d->spatials = (spatial_t*)realloc(d->spatials, (d->nspatials + SPATIALS_ALLOC) * sizeof(*d->spatials));
	d->spatials[d->nspatials] = *s;
	return d->nspatials++;
}

/* Add to hashtable */
static void
spatial_dict_addh(hash_t spatial_hash, unsigned int id)
{
	spatial_entry_t *e = malloc2(spatial_entry_t);
	e->hash = spatial_hash;
	e->id = id;
	e->next = NULL;

	uint32_t h = spatial_dict_hash(spatial_hash);
	e->next = spat_dict->hashtable[h];
	spat_dict->hashtable[h] = e;
}

unsigned int
spatial_dict_add(spatial_t *s)
{
	spatial_t *s2 = spatial_dict_lookup(s->dist, spatial_hash(0, s));
	if (s2) {
		assert(spatial_equal(s, s2));	/* Sanity check */
		return spatial_id(s2);		/* Already have */
	}

	/* Add to collection */
	unsigned int id = spatial_dict_addc(s);

	/* Add rotations to hashtable */
	for (unsigned int r = 0; r < PTH__ROTATIONS; r++)
		spatial_dict_addh(spatial_hash(r, s), id);
	return id;
}


/* Spatial dictionary file format:
 *   # comments
 *   INDEX DIST STONES
 * INDEX:  index in the spatial table
 * DIST:   @d of the pattern (radius)
 * STONES: string of ".XO#" chars   */
static void
spatial_dict_read(char *buf)
{
	/* XXX: We trust the data. Bad data will crash us. */
	char *bufp = buf;

	unsigned int index, dist;
	index = strtoul(bufp, &bufp, 10);
	dist = strtoul(bufp, &bufp, 10);
	while (isspace(*bufp)) bufp++;

	assert(dist <= MAX_PATTERN_DIST);

	/* Load the stone configuration. */
	spatial_t s = { dist, };
	unsigned int sl = 0;
	while (!isspace(*bufp)) {
		enum stone color = spatial_char2stone(*bufp++);
		set_spatial_point(&s, sl, color);
		sl++;
	}
	while (isspace(*bufp)) bufp++;

	/* Sanity check. */
	if (sl != ptind[s.dist + 1])
		die("Spatial dictionary: Invalid number of stones (%d != %d) on this line: %s\n", sl, ptind[dist + 1] - 1, buf);

	unsigned int id = spatial_dict_add(&s);
	assert(id == index);
}

void
spatial_write(spatial_t *s, unsigned int id, FILE *f)
{
	fprintf(f, "%d %d ", id, s->dist);
	fputs(spatial2str(s), f);
	fputc('\n', f);
}

static void
spatial_dict_load(FILE *f)
{
	char buf[1024];
	while (fgets(buf, sizeof(buf), f)) {
		if (buf[0] == '#') continue;
		spatial_dict_read(buf);
	}
	if (DEBUGL(1)) fprintf(stderr, "Loaded spatial dictionary of %d patterns.\n", spat_dict->nspatials);
	if (DEBUGL(3)) spatial_dict_hashstats(spat_dict);
}

static void
spatial_dict_hashstats(spatial_dict_t *dict)
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

	int stats[10] = { 0, };
	unsigned int max = 0, entries = 0, empty = 0;
	for (unsigned int i = 0; i <= spatial_hash_mask; i++) {
		unsigned int n = 0;
		for (spatial_entry_t *e = dict->hashtable[i]; e; e = e->next)  n++;
		entries += n;
		max = MAX(max, n);
		if (!n)      empty++;
		if (n < 10)  stats[n]++;
	}

	unsigned int buckets = (sizeof(dict->hashtable) / sizeof(dict->hashtable[0]));
	unsigned int htmem = sizeof(dict->hashtable);
	unsigned int mem = htmem + dict->nspatials * sizeof(spatial_t) + entries * sizeof(spatial_entry_t);
	fprintf(stderr, "Spatial hash: %i entries, empty %.1f%%, avg len %.1f,   %.1fMb (%.1fMb total)\n",
			entries,
			(float)empty * 100 / buckets,
			(float)entries / (buckets - empty),
			(float)htmem / (1024*1024), (float)mem / (1024*1024));

	if (DEBUGL(4)) {
		for (int i = 0; i < 10; i++)
			fprintf(stderr, "\t%i entries: %i (%i%%)\n", i, stats[i], stats[i] * 100 / (1 << spatial_hash_bits));
		fprintf(stderr, "\tworst case: %i entries\n", max);
	}
}

void
spatial_dict_writeinfo(FILE *f)
{
	/* New file. First, create a comment describing order
	 * of points in the array. This is just for purposes
	 * of external tools, Pachi never interprets it itself. */
	fprintf(f, "# Pachi spatial patterns dictionary v1.1 maxdist %d\n",
		MAX_PATTERN_DIST);
	for (unsigned int d = 0; d <= MAX_PATTERN_DIST; d++) {
		fprintf(f, "# Point order: d=%d ", d);
		for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			fprintf(f, "%d,%d ", ptcoords[j].x, ptcoords[j].y);
		}
		fprintf(f, "\n");
	}
}

/* Count number of spatials for each distance. */
static void
spatial_dict_index_by_dist(pattern_config_t *pc, const char *filename)
{
	assert(MAX_PATTERN_DIST == 10);
	assert(pc->spat_max == MAX_PATTERN_DIST);
	assert(pc->spat_min == 3);

	int prev_d = 0;
	for (unsigned int i = 0; i < spat_dict->nspatials; i++) {
		spatial_t *s = get_spatial(i);
		int d = s->dist;
		//fprintf(stderr, "d: %i  %s\n", d, spatial2str(s));
		if (!d) continue;
		assert(d <= MAX_PATTERN_DIST && d >= 3);
		if (d < prev_d)  die("%s: spatial dictionary must be sorted by distance\n", filename);
		
		spat_dict->nspatials_by_dist[d]++;
		if (d != prev_d)
			spat_dict->first_id[d] = i;
		
		prev_d = d;
	}
	
	for (int d = 3; d <= MAX_PATTERN_DIST; d++)
		if (DEBUGL(3)) fprintf(stderr, "Dist %i spatials: %i\n", d, spat_dict->nspatials_by_dist[d]);
}

const char *spatial_dict_filename = "patterns_mm.spat";

void
spatial_dict_init(pattern_config_t *pc, bool create)
{
	assert(!spat_dict);	
	FILE *f = fopen_data_file(spatial_dict_filename, "r");
	if (!f && !create)
		die("Pattern file %s missing, aborting.\n", spatial_dict_filename);

	spat_dict = calloc2(1, spatial_dict_t);
	/* Dummy record for index 0 so ids start at 1. */
	spatial_t dummy = { 0, };
	spatial_dict_addc(&dummy);

	if (f) {
		spatial_dict_load(f);
		spatial_dict_index_by_dist(pc, spatial_dict_filename);
		fclose(f); f = NULL;
	} else  assert(create);
}

void
spatial_dict_done()
{
	if (!spat_dict)  return;
	
	free(spat_dict->spatials);
	
	spatial_entry_t *next = NULL;
	for (unsigned int id = 0; id <= spatial_hash_mask; id++)
		for (spatial_entry_t *e = spat_dict->hashtable[id]; e ; e = next) {
			next = e->next;
			free(e);
		}

	free(spat_dict);
	spat_dict = NULL;
}
