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

/* ko_age definition was changed at commit fb09e89,
 * comment out to test against earlier versions. */
#define CHECK_KO_AGE

/* last_move3/4 was broken before a8ddcc9 */
#define CHECK_LAST_MOVES
//#define SHOW_LAST_MOVES


#define printf(...)  fprintf(stderr, __VA_ARGS__)

#include <openssl/sha.h>

#define hash_init()      \
  	int r;           \
	SHA_CTX ctx;     \
	r = SHA1_Init(&ctx);  assert(r);
#define hash_data(pt, len) do {  r = SHA1_Update(&ctx, (pt), (len));  assert(r);  } while(0)
#define hash_struct(pt)    hash_data((pt), sizeof(*(pt)));
#define hash_field(f)      hash_data(&b->f, sizeof(b->f))
#define hash_int(val)      do {  int i_ = (val);  hash_data(&i_, sizeof(i_)); } while(0)
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
	for (int col = S_NONE; col < S_MAX; col++) {
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
		
		hash_int(c);
		for (int i = 0; i < board_group_info(b, g).libs; i++)
			hash_int(board_group_info(b, g).lib[i]);
	} foreach_point_end;


	/****************************************************************/
	/* Playouts fields */

	hash_field(superko_violation);
	
	for (int i = 0; i < b->flen; i++) {
		coord_t c = b->f[i];
		hash_int(c);
		assert(b->fmap[c] == i);  /* sanity check ... */
	}

	foreach_point(b) {
		if (board_at(b, c) != S_NONE) continue;
		hash3_t h = pattern3_hash(b, c);
		hash_int(h);              /* always check pat3 is sane ... */
#ifdef BOARD_PAT3
		assert(b->pat3[c] == h);  /* sanity check ... */
#endif
	} foreach_point_end;	

	
#ifdef WANT_BOARD_C
	for (int i = 0; i < b->clen; i++) {
		hash_int(b->c[i]);
	}
#endif

	/****************************************************************/
	/* Full board fields */

	// not hashing hash history ...

	hash_field(hash);


	static unsigned char md[128];
	hash_final(md);
	return md;
}

static void
dump_board_statics(board_t *b)
{
	int size = board_rsize(b);

	/* Dump once for each board size. */
	static int size_done[BOARD_MAX_SIZE + 1] = { 0, };
	if (size_done[size])  return;
	size_done[size] = 1;

	print_board_flags(b);
	printf("board_statics(%2i)  ", size);

	printf("%38s", "");
	unsigned char *md = hash_board_statics(b);
	hash_print();

	printf("\n");
}

static void
print_move(board_t *b, move_t *m)
{
	if (m->color == S_NONE)  printf("%7s", "");
	else printf("%1.1s %-4s ", stone2str(m->color), coord2sstr(m->coord));
}

static void
dump_board(board_t *b)
{
	if (last_move(b).color == S_NONE)
		dump_board_statics(b);

	printf("move %3i ", b->moves);

	print_move(b, &last_move(b));
#ifdef SHOW_LAST_MOVES
	print_move(b, &last_move2(b));
	print_move(b, &last_move3(b));
	print_move(b, &last_move4(b));
#endif

	printf("cap %-2i %-2i ", b->captures[S_BLACK], b->captures[S_WHITE]);

	if (is_pass(b->ko.coord))  printf("%7s", "");
	else                       printf("ko %-3s ", coord2sstr(b->ko.coord));

#ifdef CHECK_KO_AGE
	if (b->last_ko_age)  printf("ko_age %-3i ", b->last_ko_age);
	else                 printf("%11s", "");
#endif

	printf("%3s ", (b->superko_violation ? "SKO" : ""));

#ifdef WANT_BOARD_C
	if (b->clen)         printf("clen %-2i  ", b->clen);
	else                 printf("%9s", "");
#endif

	unsigned char *md = hash_board(b);
	hash_print();

	printf("\n");
}

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
	while (fgets(buf, 4096, stdin)) {
		if (buf[0] == '#') continue;
		//if (!strncmp(buf, "clear_board", 11))  printf("\nGame %i:\n", gtp.played_games + 1);
		// if (DEBUGL(1))  fprintf(stderr, "IN: %s", buf);

		gtp_parse(&gtp, b, &e, ti, buf);
		dump_board(b);
		b->superko_violation = false;       // never cleared currently.
	}

	return true;
}
