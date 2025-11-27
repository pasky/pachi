#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "timeinfo.h"
#include "engine.h"
#include "gtp.h"
#include "pattern3.h"

/* Print all board structures as text ?
 * (default: print one-line summary with hash of board structures) */
//#define FULL_BOARD_DUMP


/* ko_age definition was changed at commit fb09e89,
 * comment out to test against earlier versions. */
#define CHECK_KO_AGE

/* last_move3/4 was broken before a8ddcc9 */
#define CHECK_LAST_MOVES


#define printf(...)  fprintf(stderr, __VA_ARGS__)


static void
print_board_flags(board_t *b)
{
	static int shown = 0;
	if (shown++) return;
	
	printf("board regression test:\n");
	printf("regtest flags:            ");

#ifdef BOARD_HASH_COMPAT
	printf("hcompat, ");
#endif

#ifndef CHECK_KO_AGE
	printf("!ko_age, ");
#endif

#ifndef CHECK_LAST_MOVES
	printf("!last34, ");
#endif

	// No BOARD_PAT3 flag, pat3 data is always hashed.

#ifdef WANT_BOARD_C
	printf("c");
#else
	printf("!c");
#endif

	printf("\n");
}

static void
print_move(board_t *b, move_t *m)
{
	if (m->color == S_NONE || is_pass(m->coord))  printf("%-6s", "-");
	else printf("%1.1s %-3s ", stone2str(m->color), coord2sstr(m->coord));
}


static void
print_board_header(board_t *b, bool show_last_moves)
{
	printf("move: %-3i ", b->moves);

	if (!show_last_moves) {
		printf("last: ");  print_move(b, &last_move(b));
	} else {
		printf("last moves: [ ");
		print_move(b, &last_move(b));
		print_move(b, &last_move2(b));
		print_move(b, &last_move3(b));
		print_move(b, &last_move4(b));
		printf("]  ");
	}

	printf("cap: %-2i %-2i ", b->captures[S_BLACK], b->captures[S_WHITE]);

	printf("ko: ");  print_move(b, &b->ko);

#ifdef CHECK_KO_AGE
	printf("age %-3i ", b->last_ko_age);
#endif

	printf("%3s ", (b->superko_violation ? "SKO" : ""));
}


/******************************************************************************************/
#ifdef FULL_BOARD_DUMP

/* Print once for each board size. */
static void
print_board_statics(board_t *b)
{
	int size = board_rsize(b);

	static int size_done[BOARD_MAX_SIZE + 1] = { 0, };
	if (size_done[size])
		return;
	size_done[size] = 1;

	print_board_flags(b);
	printf("board_statics(%2i)  ", size);

	printf("board_max_coords: %i\n", board_max_coords(b));
	printf("board_bits2: %i", board_bits2());

	for (int color = S_BLACK; color <= S_WHITE; color++) {
		printf("\n\nhashes (%s) (offboard):\n", stone2str(color));
		int i = 0;
		foreach_point(b) {
			if (board_at(b, c) != S_OFFBOARD)  continue;
			printf("%016" PRIhash " ", hash_at(c, color));
			if (++i % 6 == 0)
				printf("\n");
		} foreach_point_end;
	}
	
	for (int color = S_BLACK; color <= S_WHITE; color++) {
		printf("\n\nhashes (%s):\n", stone2str(color));
		int i = 0;
		foreach_point(b) {
			if (board_at(b, c) == S_OFFBOARD)  continue;
			printf("%3s %016" PRIhash " ", coord2sstr(c), hash_at(c, color));
			if (++i % 6 == 0)
				printf("\n");
		} foreach_point_end;
	}

	printf("\n\n");
}

static void
board_print_all_structures(board_t *b)
{
	int size = board_rsize(b);
	
	if (last_move(b).color == S_NONE)
		print_board_statics(b);

	board_print(b, stderr);
	
	print_board_header(b, true);

	
	/****************************************************************/
	/* Common fields */

	printf("\nsize: %i   ", size);
	// captures: already done
	// passes: already done
	printf("komi: %.1f   ", b->komi);
	printf("handicap: %i   ", b->handicap);
	printf("rules: %s   ", rules2str(b->rules));
	// moves: already done

	// last moves: already done

	// ko: already done
	printf("last_ko: ");
	print_move(b, &b->last_ko);
	// last_ko_age: already done
	printf("superko violation: %i", b->superko_violation);

	/* Neighbor info */
	for (int col = S_BLACK; col < S_MAX; col++) {  /* Not maintained for S_NONE */
		printf("\n\nneighbor count (%s):\n", (col == S_OFFBOARD ? "offboard" : stone2str(col)));
		int i = 0;
		foreach_point(b) {
			if (board_at(b, c) == S_OFFBOARD) continue;
			printf("%3s %i ", coord2sstr(c), neighbor_count_at(b, c, col));
			if (++i % 21 == 0)
				printf("\n");
		} foreach_point_end;
	}

	/* Groups */
	printf("\n\ngroups:\n");
	foreach_point_for_print(b) {
		if (board_at(b, c) == S_OFFBOARD) continue;
		group_t g = group_at(b, c);
		printf("%3s ", (g ? coord2sstr(group_at(b, c)) : "-"));
		if (coord_x(c) == board_rsize(b))
			printf("\n");
	} foreach_point_for_print_end;

	/* Group next */
	printf("\n\ngroup next:\n");
	foreach_point_for_print(b) {
		if (board_at(b, c) == S_OFFBOARD) continue;
		coord_t next = groupnext_at(b, c);
		printf("%3s ", (next ? coord2sstr(next) : "-"));
		if (coord_x(c) == board_rsize(b))
			printf("\n");
	} foreach_point_for_print_end;

	/* Group info */
	printf("\n\ngroup info:\n");
	foreach_point(b) {
		group_t g = group_at(b, c);
		group_info_t *gi = &board_group_info(b, g);
		if (!g || g != c)  continue;  /* foreach group really */
		
		printf("%3s: %i libs  ", coord2sstr(g), gi->libs);

		/* Sort liberties in canonical order so functionally
		 * equivalent implementations print identically. */
		mq_t q;  mq_init(&q);
		for (int i = 0; i < board_group_info(b, g).libs; i++)
			mq_add(&q, board_group_info(b, g).lib[i]);
		mq_sort(&q);
		mq_print(&q, "[ ");
		printf("]\n");
	} foreach_point_end;
	printf("\n");


	/****************************************************************/
	/* Playouts fields */

	// superko violation: above
	
	/* free positions:
	 * sort in canonical order so functionally
	 * equivalent implementations print the same. */
	mq_t q;  mq_init(&q);
	for (int i = 0; i < b->flen; i++) {
		coord_t c = b->f[i];
		assert(b->fmap[c] == i);  /* sanity check ... */
		mq_add(&q, c);
	}
	printf("free positions: %i\n", b->flen);
	mq_sort(&q);
	mq_print(&q, "[ ");
	printf("]\n\n");

	/* pat3 */
	printf("pat3: \n");
	int i = 0;
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;
		printf("%3s ", coord2sstr(c));
		if (board_at(b, c) != S_NONE)
			printf("%-8s  ", "-");
		else {
			hash3_t h = pattern3_hash(b, c);
			printf("%#08x  ", h);
#ifdef BOARD_PAT3
			assert(b->pat3[c] == h);  /* sanity check ... */
#endif
		}
		if (++i % 9 == 0)
			printf("\n");
	} foreach_point_end;
	printf("\n\n");

#ifdef WANT_BOARD_C
	/* Capturable groups:
	 * Sort in canonical order so functionally
	 * equivalent implementations hash identically. */
	mq_init(&q);
	for (int i = 0; i < b->clen; i++)
		mq_add(&q, b->c[i]);
	mq_sort(&q);
	printf("capturable groups: %i\n", b->clen);
	mq_print(&q, "[ ");
	printf("]\n\n");
#endif

	
	/****************************************************************/
	/* Full board fields */

	printf("board hash: %" PRIhash "\n", b->hash);

	/* Not showing hash history ... */
	
	printf("\n");
}


/******************************************************************************************/
#else /* Print board hashes only */


#include <openssl/sha.h>

#define hash_init()      \
  	int r;           \
	SHA_CTX ctx;     \
	r = SHA1_Init(&ctx);  assert(r);
#define hash_data(pt, len) do {  r = SHA1_Update(&ctx, (pt), (len));  assert(r);  } while(0)
#define hash_struct(pt)    hash_data((pt), sizeof(*(pt)));
#define hash_field(f)      hash_data(&b->f, sizeof(b->f))
#define hash_int(val)      do {  int v_ = (val);  hash_data(&v_, sizeof(v_)); } while(0)
#define hash_mq(q)	   do { \
	for (int i_ = 0; i_ < (q)->moves; i_++)	\
		hash_int((q)->move[i_]);	\
	} while(0)
#define hash_print()       do {  for (int i = 0; i < 20; i++) \
					 printf("%02x", md[i]); \
			      } while(0)
#define hash_final(md)     do {  r = SHA1_Final(md, &ctx);  assert(r);  } while(0)


static void print_board_flags(board_t *b);

static unsigned char*
hash_board_statics(board_t *b)
{
	hash_init();

	/* These are board statics really */
	hash_int(board_max_coords(b));
	hash_int(board_bits2());

	/* Just care about hashes here */
	for (int color = S_BLACK; color <= S_WHITE; color++) {
		foreach_point(b) {
			hash_t h = hash_at(c, color);
			hash_data(&h, sizeof(h));
		} foreach_point_end;
	}

	static unsigned char md[128];
	hash_final(md);
	return md;
}

static unsigned char*
hash_board(board_t *b)
{
	hash_init();

	/****************************************************************/
	/* Common fields */
	
	hash_int(board_rsize(b) + 2);  // for backwards compatibility
	//hash_field(captures);    // don't hash displayed fields
	//hash_field(passes);
	hash_field(komi);
	hash_field(handicap);
	hash_field(rules);
	//hash_field(moves);       // don't hash displayed fields

	hash_struct(&last_move(b));
	hash_struct(&last_move2(b));
#ifdef CHECK_LAST_MOVES
	hash_struct(&last_move3(b));
	hash_struct(&last_move4(b));
#endif

	//hash_field(ko);          // don't hash displayed fields
	hash_field(last_ko);
	//hash_field(last_ko_age); // don't hash displayed fields

	/* Stones */
	foreach_point(b) {
		hash_int(board_at(b, c));
	} foreach_point_end;

	/* Neighbor info */
	for (int col = S_BLACK; col < S_MAX; col++) {  /* Not maintained for S_NONE */
		foreach_point(b) {
			hash_int(neighbor_count_at(b, c, col));
		} foreach_point_end;
	}

	/* Group info */
	foreach_point(b) {
		hash_int(group_at(b, c));
	} foreach_point_end;

	foreach_point(b) {
		hash_int(groupnext_at(b, c));
	} foreach_point_end;
	
	foreach_point(b) {
		group_t g = group_at(b, c);
		if (!g || g != c)  continue;  /* foreach group really */
		
		hash_int(g);

		/* Sort liberties in canonical order before hashing so 
		 * functionally equivalent implementations hash identically. */
		mq_t q;  mq_init(&q);
		for (int i = 0; i < board_group_info(b, g).libs; i++)
			mq_add(&q, board_group_info(b, g).lib[i]);
		mq_sort(&q);
		hash_mq(&q);
	} foreach_point_end;


	/****************************************************************/
	/* Playouts fields */

	hash_field(superko_violation);

	/* Sort free positions in canonical order before hashing so
	 * functionally equivalent implementations hash identically. */
	mq_t q;  mq_init(&q);
	for (int i = 0; i < b->flen; i++) {
		coord_t c = b->f[i];
		assert(b->fmap[c] == i);  /* sanity check ... */
		mq_add(&q, c);
	}
	mq_sort(&q);
	hash_mq(&q);

	foreach_point(b) {
		if (board_at(b, c) != S_NONE) continue;
		hash3_t h = pattern3_hash(b, c);
		hash_int(h);              /* always check pat3 is sane ... */
#ifdef BOARD_PAT3
		assert(b->pat3[c] == h);  /* sanity check ... */
#endif
	} foreach_point_end;	

	
#ifdef WANT_BOARD_C
	/* Sort capturable groups in canonical order before hashing so
	 * functionally equivalent implementations hash identically. */
	mq_init(&q);
	for (int i = 0; i < b->clen; i++)
		mq_add(&q, b->c[i]);
	mq_sort(&q);
	hash_mq(&q);

#endif

	/****************************************************************/
	/* Full board fields */

	// not hashing hash history ...

	hash_field(hash);


	static unsigned char md[128];
	hash_final(md);
	return md;
}

/* Dump once for each board size. */
static void
dump_board_statics(board_t *b)
{
	int size = board_rsize(b);

	static int size_done[BOARD_MAX_SIZE + 1] = { 0, };
	if (size_done[size])  return;
	size_done[size] = 1;

	print_board_flags(b);
	printf("board_statics(%2i)  ", size);

	printf("%64s", "");
	unsigned char *md = hash_board_statics(b);
	hash_print();

	printf("\n");
}

static void
print_board_hashes(board_t *b)
{
	if (last_move(b).color == S_NONE)
		dump_board_statics(b);

	print_board_header(b, false);

	/* Show total groups, libs summary */
	int groups = 0, libs = 0;
	foreach_point(b) {
		group_t g = group_at(b, c);
		if (!g || g != c)  continue;  /* foreach group really */

		groups++;
		libs += board_group_info(b, g).libs;
	} foreach_point_end;
	printf("groups: %-2i libs %-3i ", groups, libs);

#ifdef WANT_BOARD_C
	/* Capturable groups */
 	if (b->clen)         printf("cap %-2i  ", b->clen);
	else                 printf("%8s", "");
#endif

	unsigned char *md = hash_board(b);
	hash_print();

	printf("\n");
}

#endif /* FULL_BOARD_DUMP */


/******************************************************************************************/


/* Replay games dumping board state hashes at every move. */
bool
board_regression_test(board_t *b, char *arg)
{
	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_none;
	ti[S_WHITE] = ti_none;

	gtp_t gtp;  gtp_init(&gtp, b);
	char buf[4096];
	engine_t e;  memset(&e, 0, sizeof(e));  /* dummy engine */
	
	for (int lineno = 1; fgets(buf, 4096, stdin); lineno++) {
		if (buf[0] == '#') continue;
		//if (!strncmp(buf, "clear_board", 11))  printf("\nGame %i:\n", gtp.played_games + 1);
		if (DEBUGL(2))  fprintf(stderr, "IN: %s", buf);

		enum parse_code c = gtp_parse(&gtp, b, &e, ti, buf);
		if (gtp.error || (c != P_OK && c != P_ENGINE_RESET))
			die("stdin:%i  gtp command '%s' failed, aborting.\n", lineno, buf);

		if (DEBUGL(2))  board_print(b, stderr);

#ifdef FULL_BOARD_DUMP
		board_print_all_structures(b);
#else
		print_board_hashes(b);
#endif
		b->superko_violation = false;       // never cleared currently.
	}

	return true;
}
