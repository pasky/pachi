#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "move.h"
#include "timeinfo.h"
#include "gtp.h"
#include "joseki.h"
#include "engine.h"
#include "dcnn.h"
#include "tactics/util.h"
#include "engines/josekiscan.h"

static bool joseki_enabled = true;
static bool joseki_required = false;
void disable_joseki()  {  joseki_enabled = false;  }
void require_joseki()  {  joseki_required = true;  }


joseki_dict_t *joseki_dict = NULL;

/* Joseki component only used in mcts only mode (no dcnn) for now. */
bool
using_joseki(board_t *b)
{
	bool r = (joseki_enabled && !using_dcnn(b) &&
		  joseki_dict && joseki_dict->bsize == board_size(b));
	if (joseki_required && !r)  die("joseki required but not used, aborting.\n");
	return r;
}

static joseki_dict_t *
joseki_init(int bsize)
{
	joseki_dict_t *jd = calloc2(1, joseki_dict_t);
	jd->bsize = bsize;
	return jd;
}

static josekipat_t *
joseki_pattern_new(board_t *b, coord_t coord, enum stone color, josekipat_t *prev, int flags)
{
	josekipat_t *p = calloc2(1, josekipat_t);
	p->coord = coord;
	p->color = color;
	p->flags = flags;
	if (flags & JOSEKI_FLAGS_3X3)  p->h = joseki_3x3_spatial_hash(b, coord, color);
	else			       p->h = joseki_spatial_hash(b, coord, color);
	p->prev = prev;
	return p;
}

static uint32_t
joseki_dict_hash(hash_t h, coord_t coord)
{
	return ((uint32_t)h ^ coord) & joseki_hash_mask;
	//return ((uint32_t)(h >> 32) ^ (uint32_t)(h & 0xffffffff) ^ coord) & joseki_hash_mask;
}

static bool
joseki_dict_equal(josekipat_t *p1, josekipat_t *p2)
{
	return (p1->coord == p2->coord && 
		p1->color == p2->color &&
		p1->h     == p2->h);		/* don't check flags, used for lookup */
}

static bool
flags_match(josekipat_t *p1, josekipat_t *p2)
{
#define JOSEKI_FLAGS_MASK (JOSEKI_FLAGS_3X3 | JOSEKI_FLAGS_LATER)
	return ((p1->flags & JOSEKI_FLAGS_MASK) == (p2->flags & JOSEKI_FLAGS_MASK));
}

/* Same logic as joseki_prev_matches() */
static int
same_prevs(josekipat_t *prev1, josekipat_t *prev2)
{
	if ((prev1 != NULL) != (prev2 != NULL))  return false;
	if (!prev1 && !prev2)                    return true;
	if (!joseki_dict_equal(prev1, prev2))    return false;
	/* Don't care about IGNORE / LATER flags. */
	if ((prev1->flags & JOSEKI_FLAGS_3X3) != (prev2->flags & JOSEKI_FLAGS_3X3))  return false;
	if ((prev1->flags & JOSEKI_FLAGS_3X3))
		return same_prevs(prev1->prev, prev2->prev);
	return true;
}

static bool joseki_prev_matches(board_t *b, josekipat_t *prev);

static josekipat_t*
joseki_lookup_regular_prev(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color,
			   josekipat_t *prev, int flags)
{
	hash_t h = joseki_spatial_hash(b, coord, color);
	uint32_t kh = joseki_dict_hash(h, coord);
	josekipat_t p1 = josekipat(coord, color, h, prev, flags);
	for (josekipat_t *p = jd->hash[kh]; p; p = p->next) {
		if (!joseki_dict_equal(&p1, p))  continue;
		if (!flags_match(&p1, p))        continue;
		if (!same_prevs(p->prev, prev))  continue;
		assert(joseki_prev_matches(b, p->prev));
		return p;
	}
	return NULL;
}

static josekipat_t*
joseki_lookup_3x3_prev(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color,
		       josekipat_t *prev, int flags)
{
	hash_t h = joseki_3x3_spatial_hash(b, coord, color);
	josekipat_t p1 = josekipat(coord, color, h, prev, flags);
	for (josekipat_t *p = jd->pat_3x3[color]; p; p = p->next) {
		if (!joseki_dict_equal(&p1, p))        continue;
		if (!flags_match(&p1, p))              continue;
		if (!same_prevs(p->prev, prev))        continue;
		assert(joseki_prev_matches(b, p->prev));
		return p;
	}
	return NULL;
}

static josekipat_t*
joseki_lookup_ignored_prev(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color, josekipat_t *prev)
{
	hash_t h  = joseki_spatial_hash(b, coord, color);
	hash_t h3 = joseki_3x3_spatial_hash(b, coord, color);

	josekipat_t p1 = josekipat(coord, color, h,  prev, 0);
	josekipat_t p2 = josekipat(coord, color, h3, prev, 0);
	for (josekipat_t *p = jd->ignored; p; p = p->next) {
		// should check flags and compare only one ...
		if (!joseki_dict_equal(&p1, p) && !joseki_dict_equal(&p2, p))  continue;
		if (!same_prevs(p->prev, prev))        continue;
		assert(joseki_prev_matches(b, p->prev));
		return p;
	}
	return NULL;
}

static josekipat_t *
joseki_add_ignored(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color, josekipat_t *prev, int flags)
{
	josekipat_t *p = joseki_lookup_ignored_prev(jd, b, coord, color, prev);
	if (p)  return p;

	p = joseki_pattern_new(b, coord, color, prev, flags);
	p->next = jd->ignored;
	jd->ignored = p;
	return p;
}

static josekipat_t *
joseki_add_3x3(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color, josekipat_t *prev, int flags)
{
	assert(!is_pass(coord));
	if (!prev)  die("joseki: [ %s %s ] adding 3x3 match with no previous move, this is bad.\n",
			coord2sstr(last_move(b).coord), coord2sstr(coord));
	josekipat_t *p = joseki_lookup_3x3_prev(jd, b, coord, color, prev, flags);
	if (p)  return p;

	p = joseki_pattern_new(b, coord, color, prev, flags);
	p->next = jd->pat_3x3[color];
	jd->pat_3x3[color] = p;
	return p;
}

josekipat_t *
joseki_add(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color, josekipat_t *prev, int flags)
{
	/* Pattern can be both ignored and 3x3 */
	if (flags & JOSEKI_FLAGS_IGNORE)  return joseki_add_ignored(jd, b, coord, color, prev, flags);
	if (flags & JOSEKI_FLAGS_3X3)     return joseki_add_3x3(jd, b, coord, color, prev, flags);

	josekipat_t *p = joseki_lookup_regular_prev(jd, b, coord, color, prev, flags);
	if (p)  return p;
	
	p = joseki_pattern_new(b, coord, color, prev, flags);
	uint32_t kh = joseki_dict_hash(p->h, coord);
	p->next = jd->hash[kh];
	jd->hash[kh] = p;
	return p;
}

static void
joseki_stats(joseki_dict_t *jd)
{
	int normal = 0, relaxed = 0, ignored = 0, later = 0;
	forall_joseki_patterns(jd)         {  normal++;  if (p->flags & JOSEKI_FLAGS_LATER)  later++;  }
	forall_3x3_joseki_patterns(jd)     {  relaxed++; if (p->flags & JOSEKI_FLAGS_LATER)  later++;  }
	forall_ignored_joseki_patterns(jd) {  ignored++; if (p->flags & JOSEKI_FLAGS_LATER)  later++;  }

	/* hashtable stats */
	unsigned int worst = 0, entries = 0, empty = 0, buckets = (1 << joseki_hash_bits);
	for (unsigned int i = 0; i < buckets; i++) {
		unsigned int n = 0;
		for (josekipat_t *p = jd->hash[i]; p; p = p->next)  n++;
		worst = MAX(worst, n);
		if (!n)  empty++;
		entries += n;
	}

	unsigned int memht = buckets * sizeof(void*);
	unsigned int mem = memht + (normal + relaxed + ignored) * sizeof(josekipat_t);
	fprintf(stderr, "Joseki dict: %-5i moves,  3x3: %-5i  ignored: %-5i  later: %-5i   %.1fMb total\n", normal, relaxed, ignored, later, (float)mem / (1024*1024));
	fprintf(stderr, "       hash: %-5i entries, empty %2i%%, avg len %.1f, worst %2i,         %.1fMb\n",
		entries, empty * 100 / buckets,
		(float)entries / (buckets - empty), worst, (float)memht / (1024*1024));
}

static char *abcd = "abcdefghjklmnopqrstuvwxyz";

/* Hack: make 19x19 joseki work for other boardsizes.
 * XXX assumes all sequences start in top-right corner ... */
static int
convert_coords(int bsize, char *buf)
{
	if (str_prefix("boardsize", buf))
		sprintf(buf, "boardsize %i", bsize-2);	
	
	if (str_prefix("play ", buf)) {  /* Convert coordinates */
		char *arg = buf + 7;  assert(buf[6] == ' ');
		if (str_prefix("pass", arg))  return 0;
		
		coord_t c = str2coord_for(arg, 19+2);
		int offset = 21 - bsize;  assert(offset >= 0);
		int x = (c % 21) - offset,  y = (c / 21) - offset;		
		if (x < 1 || y < 1)           return -1;	/* Offboard, discard rest of sequence */
		
		char end = arg[strcspn(arg, " \n")];
		int n = sprintf(arg, "%c%i", abcd[x-1], y);
		arg[n] = end;
	}
	return 0;
}

static void
skip_sequence(char *buf, int len, FILE *f, int *lineno)
{
	*buf = 0;
	for (; fgets(buf, len, f); (*lineno)++)
		if (str_prefix("clear_board", buf))
			return;
}

/* Load joseki database.
 * For board sizes between 13x13 and 19x19 try to convert coordinates. */
void
joseki_load(int bsize)
{
	if (!joseki_enabled)  return;
	if (joseki_dict && joseki_dict->bsize != bsize)  joseki_done();
	if (joseki_dict && joseki_dict->bsize == bsize)  return;
	if (joseki_dict || bsize < 13+2)  return;  /* no joseki below 13x13 */

	char fname[1024];
	snprintf(fname, 1024, "joseki19.gtp");
	FILE *f = fopen_data_file(fname, "r");
	if (!f) {
		if (DEBUGL(3))  perror(fname);
		if (joseki_required)  die("joseki required but joseki19.gtp not found, aborting.\n");
		return;  
	}

	joseki_dict = joseki_init(bsize);

	int saved_debug_level = debug_level;
	debug_level = 0;   /* quiet */
	board_t *b = board_new(bsize, NULL);
	engine_t e;  engine_init(&e, E_JOSEKISCAN, NULL, NULL);
	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_none;
	ti[S_WHITE] = ti_none;
	char buf[4096];
	gtp_t gtp;  gtp_init(&gtp);
	for (int lineno = 1; fgets(buf, 4096, f); lineno++) {
		if (bsize != 19+2 && convert_coords(bsize, buf) < 0)
			skip_sequence(buf, 4096, f, &lineno);

		gtp.quiet = true;
		enum parse_code c = gtp_parse(&gtp, b, &e, NULL, ti, buf);  /* quiet */
		/* TODO check gtp command didn't gtp_error() also, will still return P_OK on error ... */
		if (c != P_OK && c != P_ENGINE_RESET)
			die("%s:%i  gtp command '%s' failed, aborting.\n", fname, lineno, buf);		
	}
	engine_done(&e);
	board_done(b);
	debug_level = saved_debug_level;
	int variations = gtp.played_games;
	
	if (DEBUGL(2))  fprintf(stderr, "Loaded joseki dictionary for %ix%i (%i variations).\n", bsize-2, bsize-2, variations);
	if (DEBUGL(3))  joseki_stats(joseki_dict);
	fclose(f);
}

void
joseki_done()
{
	if (!joseki_dict) return;
	
	josekipat_t *prev = NULL;
	forall_joseki_patterns(joseki_dict)         {  free(prev);  prev = p;  }
	forall_3x3_joseki_patterns(joseki_dict)     {  free(prev);  prev = p;  }
	forall_ignored_joseki_patterns(joseki_dict) {  free(prev);  prev = p;  }
	free(prev);
	free(joseki_dict);
	joseki_dict = NULL;
}

static float
joseki_rating(board_t *b, josekipat_t *p)
{
	coord_t prev = (p->prev ? p->prev->coord : pass);
	coord_t last = last_move(b).coord;
	if (b->moves < 4)		     return 0.2; /* Play corners first */
	if (p->flags & JOSEKI_FLAGS_LATER)   return 0.2; /* Low prio */
	if (prev == last && last != pass)    return 1.0; /* Boost answers to last move */
	if (prev != pass)                    return 0.5; /* Continue interrupted joseki */
	return 0.2;
}

static bool
joseki_prev_matches(board_t *b, josekipat_t *prev)
{
	if (!prev)  return true;
	if (board_at(b, prev->coord) != prev->color)  return false;
	
	/* If 3x3 prev, continue until we have a full match ...*/
	if (prev->flags & JOSEKI_FLAGS_3X3) {
		if (prev->h != joseki_3x3_spatial_hash(b, prev->coord, prev->color))  return false;
		
		/* hack, won't work if there are captures ... */
		enum stone tmp = board_at(b, prev->coord);  board_at(b, prev->coord) = S_NONE;
		bool r = joseki_prev_matches(b, prev->prev);
		board_at(b, prev->coord) = tmp;
		return r;
	}
	
	return (prev->h == joseki_spatial_hash(b, prev->coord, prev->color));
}

/* XXX can be several matches for one move in case multiple prev moves lead here.
 * we only return first match, however prefer strong matches over weak matches
 * and last move matches above all else. */ 
static josekipat_t*
joseki_lookup_regular(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color)
{
	hash_t h = joseki_spatial_hash(b, coord, color);
	uint32_t kh = joseki_dict_hash(h, coord);

	josekipat_t *match_low = NULL, *match_prev = NULL, *match_any = NULL;
	josekipat_t p1 = josekipat(coord, color, h, NULL, 0);
	for (josekipat_t *p = jd->hash[kh]; p; p = p->next) {
		josekipat_t *prev = p->prev;
		if (!joseki_dict_equal(&p1, p))     continue;
		if (!joseki_prev_matches(b, prev))  continue;
		
		if (!prev)  {  match_any = p;  continue;  }		/* weak match: no previous move */
		
		if (p->flags & JOSEKI_FLAGS_LATER)  match_low = p;	/* low prio */
		else				    match_prev = p;	/* strong match: prev move matches */

		if (prev->coord == last_move(b).coord &&
		    prev->color == last_move(b).color)
			return p;					/* last move match */
	}

	return (match_prev ? match_prev : (match_low ? match_low : match_any));
}

/* same as joseki_lookup_regular(): prefer last move matches above all else. */ 
josekipat_t*
joseki_lookup_3x3(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color)
{
	hash_t h = joseki_3x3_spatial_hash(b, coord, color);
	josekipat_t p1 = josekipat(coord, color, h, NULL, 0);
	josekipat_t *match_low = NULL, *match_prev = NULL;
	for (josekipat_t *p = jd->pat_3x3[color]; p; p = p->next) {
		josekipat_t *prev = p->prev;
		if (!joseki_dict_equal(&p1, p))     continue;
		if (!joseki_prev_matches(b, prev))  continue;
		
		/* no weak matches for 3x3 */

		if (p->flags & JOSEKI_FLAGS_LATER)  match_low = p;	/* low prio */			
		else				    match_prev = p;	/* strong match: prev move matches */

		if (prev->coord == last_move(b).coord &&
		    prev->color == last_move(b).color)
			return p;					/* last move match */
	}
	return (match_prev ? match_prev : match_low);
}

josekipat_t*
joseki_lookup_ignored(joseki_dict_t *jd, board_t *b, coord_t coord, enum stone color)
{
	hash_t h  = joseki_spatial_hash(b, coord, color);
	hash_t h3 = joseki_3x3_spatial_hash(b, coord, color);

	josekipat_t p1 = josekipat(coord, color, h,  NULL, 0);
	josekipat_t p2 = josekipat(coord, color, h3, NULL, 0);
	for (josekipat_t *p = jd->ignored; p; p = p->next) {
		// should check flags and compare only one ...
		if (!joseki_dict_equal(&p1, p) && !joseki_dict_equal(&p2, p))  continue;
		if (joseki_prev_matches(b, p->prev))  return p;
	}
	return NULL;
}

static int
append_3x3_matches(joseki_dict_t *jd, board_t *b, enum stone color,
		   coord_t *coords, float *ratings, int matches)
{
	for (josekipat_t *p = jd->pat_3x3[color]; p; p = p->next) {
		if (board_at(b, p->coord) != S_NONE)  continue;
		if (p->h != joseki_3x3_spatial_hash(b, p->coord, color))  continue;
		if (!joseki_prev_matches(b, p->prev))  continue;
		
		float rating = joseki_rating(b, p);
		for (int i = 0; i < matches; i++) {
			if (coords[i] != p->coord)  continue;
			ratings[i] = MAX(ratings[i], rating);
			goto done;
		}

		coords[matches] = p->coord;
		ratings[matches++] = rating;
	done:	continue;
	}

	return matches;
}

int
joseki_list_moves(joseki_dict_t *jd, board_t *b, enum stone color,
                  coord_t *coords, float *ratings)
{
	assert(using_joseki(b));
	int matches = 0;
	
	foreach_free_point(b) {
		josekipat_t *p = joseki_lookup_regular(jd, b, c, color);
		if (!p)  continue;
		
		coords[matches] = c;
		ratings[matches++] = joseki_rating(b, p);
	} foreach_free_point_end;

	return append_3x3_matches(jd, b, color, coords, ratings, matches);
}

void
joseki_rate_moves(joseki_dict_t *jdict, board_t *b, enum stone color,
                  float *map)
{
	coord_t coords[BOARD_MAX_COORDS];
	float ratings[BOARD_MAX_COORDS];
	int n = joseki_list_moves(jdict, b, color, coords, ratings);

	for (int i = 0; i < BOARD_MAX_COORDS; i++)
		map[i] = 0.0;
	for (int i = 0; i < n; i++)
		map[coords[i]] = ratings[i];
}

void
get_joseki_best_moves(board_t *b, coord_t *coords, float *ratings, int matches,
		       coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}
	
	for (int i = 0; i < matches; i++)
		best_moves_add(coords[i], ratings[i], best_c, best_r, nbest);
}

void
print_joseki_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest)
{
	int cols = best_moves_print(b, "joseki =   ", best_c, nbest);	

	fprintf(stderr, "%*s[ ", cols, "");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3i ", (int)(best_r[i] * 100.0));
	fprintf(stderr, "]\n");
}

void
print_joseki_moves(joseki_dict_t *jdict, board_t *b, enum stone color)
{
	if (!using_joseki(b))  return;
	
	coord_t coords[BOARD_MAX_COORDS];
	float ratings[BOARD_MAX_COORDS];
	int n = joseki_list_moves(jdict, b, color, coords, ratings);
	if (!n)  return;

	int nbest = 20;
	float best_r[20] = { 0.0, };
	coord_t best_c[20];
	get_joseki_best_moves(b, coords, ratings, n, best_c, best_r, nbest);
	print_joseki_best_moves(b, best_c, best_r, nbest);
}


