#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG
#include "board.h"
#include "debug.h"
#include "fbook.h"
#include "mq.h"
#include "random.h"

#ifdef BOARD_PAT3
#include "pattern3.h"
#endif

#if 0
#define profiling_noinline __attribute__((noinline))
#else
#define profiling_noinline
#endif

#define gi_granularity 4
#define gi_allocsize(gids) ((1 << gi_granularity) + ((gids) >> gi_granularity) * (1 << gi_granularity))

static inline void
board_addf(struct board *b, coord_t c)
{
	b->fmap[c] = b->flen; 
	b->f[b->flen++] = c;
}

static inline void
board_rmf(struct board *b, int f)
{
	/* Not bothering to delete fmap records,
	 * Just keep the valid ones up to date. */
	coord_t c = b->f[f] = b->f[--b->flen];
	b->fmap[c] = f;
}

static void
board_setup(struct board *b)
{
	memset(b, 0, sizeof(*b));

	struct move m = { pass, S_NONE };
	b->last_move = b->last_move2 = b->last_move3 = b->last_move4 = b->last_ko = b->ko = m;
}

void
board_init(struct board *b, int bsize, char *fbookfile)
{
	board_setup(b);
	b->fbookfile = fbookfile;
	b->size = bsize;
	board_clear(b);	
}

struct board *
board_new(int bsize, char *fbookfile)
{
	struct board *b = malloc2(sizeof(struct board));
	board_init(b, bsize, fbookfile);
	return b;
}

int
board_cmp(struct board *b1, struct board *b2)
{
	return memcmp(b1, b2, sizeof(struct board));
}

int
board_quick_cmp(struct board *b1, struct board *b2)
{
	if (b1->size != b2->size ||
	    b1->size2 != b2->size2 ||
	    b1->bits2 != b2->bits2 ||
	    b1->captures[S_BLACK] != b2->captures[S_BLACK] ||
	    b1->captures[S_WHITE] != b2->captures[S_WHITE] ||
	    b1->moves != b2->moves) {
		fprintf(stderr, "differs in main vars\n");
		return 1;
	}
	if (move_cmp(&b1->last_move, &b2->last_move) ||
	    move_cmp(&b1->last_move2, &b2->last_move2)) {
		fprintf(stderr, "differs in last_move\n");
		return 1;
	}
	if (move_cmp(&b1->ko, &b2->ko) ||
	    move_cmp(&b1->last_ko, &b2->last_ko) ||
	    b1->last_ko_age != b2->last_ko_age) {
		fprintf(stderr, "differs in ko\n");
		return 1;
	}

	if (memcmp(b1->b,  b2->b,  sizeof(b1->b))) {
		fprintf(stderr, "differs in b\n");  return 1;  }
	if (memcmp(b1->g,  b2->g,  sizeof(b1->g))) {
		fprintf(stderr, "differs in g\n");  return 1;  }
	if (memcmp(b1->n,  b2->n,  sizeof(b1->n))) {
		fprintf(stderr, "differs in n\n");  return 1;  }
	if (memcmp(b1->p,  b2->p,  sizeof(b1->p))) {
		fprintf(stderr, "differs in p\n");  return 1;  }
	if (memcmp(b1->gi, b2->gi, sizeof(b1->gi))) {
		fprintf(stderr, "differs in gi\n");  return 1;  }

	return 0;
}


struct board *
board_copy(struct board *b2, struct board *b1)
{
	memcpy(b2, b1, sizeof(struct board));

	// XXX: Special semantics.
	b2->fbook = NULL;
	b2->ps = NULL;

	return b2;
}

void
board_done_noalloc(struct board *board)
{
	if (board->fbook) fbook_done(board->fbook);
	if (board->ps) free(board->ps);
}

void
board_done(struct board *board)
{
	board_done_noalloc(board);
	free(board);
}

void
board_resize(struct board *board, int size)
{
#ifdef BOARD_SIZE
	assert(board_size(board) == size + 2);
#endif
	assert(size <= BOARD_MAX_SIZE);
	board->size = size + 2 /* S_OFFBOARD margin */;
	board->size2 = board_size(board) * board_size(board);

	board->bits2 = 1;
	while ((1 << board->bits2) < board->size2) board->bits2++;
}

struct board_statics board_statics = { .size = 0 };

static void
board_statics_init(struct board *board)
{
	int size = board_size(board);
	struct board_statics *bs = &board_statics;
	if (bs->size == size)
		return;
	
	memset(bs, 0, sizeof(*bs));
	bs->size = size;
	
	/* Setup neighborhood iterators */
	bs->nei8[0] = -size - 1; // (-1,-1)
	bs->nei8[1] = 1;
	bs->nei8[2] = 1;
	bs->nei8[3] = size - 2; // (-1,0)
	bs->nei8[4] = 2;
	bs->nei8[5] = size - 2; // (-1,1)
	bs->nei8[6] = 1;
	bs->nei8[7] = 1;
	bs->dnei[0] = -size - 1;
	bs->dnei[1] = 2;
	bs->dnei[2] = size*2 - 2;
	bs->dnei[3] = 2;

	/* Set up coordinate cache */
	foreach_point(board) {
		bs->coord[c][0] = c % board_size(board);
		bs->coord[c][1] = c / board_size(board);
	} foreach_point_end;

	/* Initialize zobrist hashtable. */
	/* We will need these to be stable across Pachi runs for
	 * certain kinds of pattern matching, thus we do not use
	 * fast_random() for this. */
	hash_t hseed = 0x3121110101112131;

	/* XXX Until <board_cleanup> board->h was treated as:
	 *     h[2][BOARD_MAX_COORDS] here, and
	 *     h[BOARD_MAX_COORDS][2] in hash_at().
	 *     Preserve quirk to get same hashes across versions... */
	hash_t (*hash)[2] = (hash_t (*)[2])bs->h;
	for (coord_t c = 0 ; c < BOARD_MAX_COORDS; c++) {  /* Don't foreach_point(), need all 21x21 */
		hash[c][0] = (hseed *= 16807);
		if (!hash[c][0])  hash[c][0] = 1;
		
		/* And once again for white */
		hash[c][1] = (hseed *= 16807);
		if (!hash[c][1])  hash[c][1] = 1;
	}

	/* Sanity check ... */
	foreach_point(board) {
		assert(hash_at(b, c, S_BLACK) != 0);
		assert(hash_at(b, c, S_WHITE) != 0);
	} foreach_point_end;
}

static void
board_init_data(struct board *board)
{
	int size = board_size(board);

	board_setup(board);
	board_resize(board, size - 2 /* S_OFFBOARD margin */);

	/* Setup initial symmetry */
	if (size % 2) {
		board->symmetry.d = 1;
		board->symmetry.x1 = board->symmetry.y1 = board_size(board) / 2;
		board->symmetry.x2 = board->symmetry.y2 = board_size(board) - 1;
		board->symmetry.type = SYM_FULL;
	} else {
		/* TODO: We do not handle board symmetry on boards
		 * with no tengen yet. */
		board->symmetry.d = 0;
		board->symmetry.x1 = board->symmetry.y1 = 1;
		board->symmetry.x2 = board->symmetry.y2 = board_size(board) - 1;
		board->symmetry.type = SYM_NONE;
	}

	/* Draw the offboard margin */
	int top_row = board_size2(board) - board_size(board);
	int i;
	for (i = 0; i < board_size(board); i++)
		board->b[i] = board->b[top_row + i] = S_OFFBOARD;
	for (i = 0; i <= top_row; i += board_size(board))
		board->b[i] = board->b[board_size(board) - 1 + i] = S_OFFBOARD;

	foreach_point(board) {
		coord_t coord = c;
		if (board_at(board, coord) == S_OFFBOARD)
			continue;
		foreach_neighbor(board, c, {
			inc_neighbor_count_at(board, coord, board_at(board, c));
		} );
	} foreach_point_end;

	/* All positions are free! Except the margin. */
	for (i = board_size(board); i < (board_size(board) - 1) * board_size(board); i++)
		if (i % board_size(board) != 0 && i % board_size(board) != board_size(board) - 1)
			board_addf(board, i);

#ifdef BOARD_PAT3
	/* Initialize 3x3 pattern codes. */
	foreach_point(board) {
		if (board_at(board, c) == S_NONE)
			board->pat3[c] = pattern3_hash(board, c);
	} foreach_point_end;
#endif
}

void
board_clear(struct board *board)
{
	int size = board_size(board);
	floating_t komi = board->komi;
	char *fbookfile = board->fbookfile;
	enum go_ruleset rules = board->rules;

	board_done_noalloc(board);

	board_statics_init(board);
	static struct board bcache[BOARD_MAX_SIZE + 2];
	assert(size > 0 && size <= BOARD_MAX_SIZE + 2);
	if (bcache[size - 1].size == size)
		board_copy(board, &bcache[size - 1]);
	else {
		board_init_data(board);
		board_copy(&bcache[size - 1], board);
	}

	board->komi = komi;
	board->fbookfile = fbookfile;
	board->rules = rules;

	if (board->fbookfile)
		board->fbook = fbook_init(board->fbookfile, board);
}

static void
board_print_top(struct board *board, strbuf_t *buf, int c)
{
	for (int i = 0; i < c; i++) {
		char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
		sbprintf(buf, "      ");
		for (int x = 1; x < board_size(board) - 1; x++)
			sbprintf(buf, "%c ", asdf[x - 1]);
		sbprintf(buf, " ");
	}
	sbprintf(buf, "\n");
	for (int i = 0; i < c; i++) {
		sbprintf(buf, "    +-");
		for (int x = 1; x < board_size(board) - 1; x++)
			sbprintf(buf, "--");
		sbprintf(buf, "+");
	}
	sbprintf(buf, "\n");
}

static void
board_print_bottom(struct board *board, strbuf_t *buf, int c)
{
	for (int i = 0; i < c; i++) {
		sbprintf(buf, "    +-");
		for (int x = 1; x < board_size(board) - 1; x++)
			sbprintf(buf, "--");
		sbprintf(buf, "+");
	}
	sbprintf(buf, "\n");
}

static void
board_print_row(struct board *board, int y, strbuf_t *buf, board_cprint cprint, void *data)
{
	sbprintf(buf, " %2d | ", y);
	for (int x = 1; x < board_size(board) - 1; x++)
		if (coord_x(board->last_move.coord, board) == x && coord_y(board->last_move.coord, board) == y)
			sbprintf(buf, "%c)", stone2char(board_atxy(board, x, y)));
		else
			sbprintf(buf, "%c ", stone2char(board_atxy(board, x, y)));
	sbprintf(buf, "|");
	if (cprint) {
		sbprintf(buf, " %2d | ", y);
		for (int x = 1; x < board_size(board) - 1; x++)
			cprint(board, coord_xy(board, x, y), buf, data);
		sbprintf(buf, "|");
	}
	sbprintf(buf, "\n");
}

void
board_print_custom(struct board *board, FILE *f, board_cprint cprint, void *data)
{
	char buffer[10240];
	strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	sbprintf(buf, "Move: % 3d  Komi: %2.1f  Handicap: %d  Captures B: %d W: %d  ",
		 board->moves, board->komi, board->handicap,
		 board->captures[S_BLACK], board->captures[S_WHITE]);
	if (cprint) /* handler can add things to header when called with pass */
		cprint(board, pass, buf, data);
	sbprintf(buf, "\n");
	board_print_top(board, buf, 1 + !!cprint);
	for (int y = board_size(board) - 2; y >= 1; y--)
		board_print_row(board, y, buf, cprint, data);
	board_print_bottom(board, buf, 1 + !!cprint);
	fprintf(f, "%s\n", buf->str);
}

static void
board_hprint_row(struct board *board, int y, strbuf_t *buf, board_print_handler handler, void *data)
{
        sbprintf(buf, " %2d | ", y);
        for (int x = 1; x < board_size(board) - 1; x++) {
                char *stone_str = handler(board, coord_xy(board, x, y), data);
                if (coord_x(board->last_move.coord, board) == x && coord_y(board->last_move.coord, board) == y)
                        sbprintf(buf, "%s)", stone_str);
                else
                        sbprintf(buf, "%s ", stone_str);
        }
        sbprintf(buf, "|\n");
}

void
board_hprint(struct board *board, FILE *f, board_print_handler handler, void *data)
{
        char buffer[10240];
	strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
        sbprintf(buf, "Move: % 3d  Komi: %2.1f  Handicap: %d  Captures B: %d W: %d\n",
		 board->moves, board->komi, board->handicap,
		 board->captures[S_BLACK], board->captures[S_WHITE]);
	board_print_top(board, buf, 1);
        for (int y = board_size(board) - 2; y >= 1; y--)
                board_hprint_row(board, y, buf, handler, data);
        board_print_bottom(board, buf, 1);
        fprintf(f, "%s\n", buf->str);
}

static void
cprint_group(struct board *board, coord_t c, strbuf_t *buf, void *data)
{
	sbprintf(buf, "%d ", group_base(group_at(board, c)));
}

void
board_print(struct board *board, FILE *f)
{
	board_print_custom(board, f, DEBUGL(6) ? cprint_group : NULL, NULL);
}

static char*
print_target_move_handler(struct board *b, coord_t c, void *data)
{	
	static char buf[32];
	coord_t target_move = (coord_t)data;

	if (c == target_move)	sprintf(buf, "\e[40;33;1m*\e[0m");
	else			sprintf(buf, "%c", stone2char(board_at(b, c)));
	return buf;
}

void
board_print_target_move(struct board *b, FILE *f, coord_t target_move)
{
	assert(!is_pass(target_move));
	assert(board_at(b, target_move) == S_NONE);
	board_hprint(b, f, print_target_move_handler, (void*)target_move);
}


/* Update board hash with given coordinate. */
static void profiling_noinline
board_hash_update(struct board *board, coord_t coord, enum stone color)
{
	board->hash ^= hash_at(board, coord, color);
#ifdef JOSEKI
	board->qhash[coord_quadrant(coord, board)] ^= hash_at(board, coord, color);
#endif
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_update(%d,%d,%d) ^ %"PRIhash" -> %"PRIhash"\n", color, coord_x(coord, board), coord_y(coord, board), hash_at(board, coord, color), board->hash);

#if defined(BOARD_PAT3)
	/* @color is not what we need in case of capture. */
	static const int ataribits[8] = { -1, 0, -1, 1, 2, -1, 3, -1 };
	enum stone new_color = board_at(board, coord);
	bool in_atari = false;
	if (new_color == S_NONE)
		board->pat3[coord] = pattern3_hash(board, coord);
	else
		in_atari = (board_group_info(board, group_at(board, coord)).libs == 1);
	foreach_8neighbor(board, coord) {
		/* Internally, the loop uses fn__i=[0..7]. We can use
		 * it directly to address bits within the bitmap of the
		 * neighbors since the bitmap order is reverse to the
		 * loop order. */
		if (board_at(board, c) != S_NONE)
			continue;
		board->pat3[c] &= ~(3 << (fn__i*2));
		board->pat3[c] |= new_color << (fn__i*2);
		if (ataribits[fn__i] >= 0) {
			board->pat3[c] &= ~(1 << (16 + ataribits[fn__i]));
			board->pat3[c] |= in_atari << (16 + ataribits[fn__i]);
		}
	} foreach_8neighbor_end;
#endif
}

/* Commit current board hash to history. */
static void profiling_noinline
board_hash_commit(struct board *board)
{
	if (DEBUGL(8))
		fprintf(stderr, "board_hash_commit %"PRIhash"\n", board->hash);
	if (likely(board->history_hash[board->hash & history_hash_mask]) == 0) {
		board->history_hash[board->hash & history_hash_mask] = board->hash;
		return;
	}

	hash_t i = board->hash;
	while (board->history_hash[i & history_hash_mask]) {
		if (board->history_hash[i & history_hash_mask] == board->hash) {
			if (DEBUGL(5))
				fprintf(stderr, "SUPERKO VIOLATION noted at %d,%d\n",
					coord_x(board->last_move.coord, board), coord_y(board->last_move.coord, board));
			board->superko_violation = true;
			return;
		}
		i = history_hash_next(i);
	}
	board->history_hash[i & history_hash_mask] = board->hash;
}


void
board_symmetry_update(struct board *b, struct board_symmetry *symmetry, coord_t c)
{
	if (likely(symmetry->type == SYM_NONE)) {
		/* Fully degenerated already. We do not support detection
		 * of restoring of symmetry, assuming that this is too rare
		 * a case to handle. */
		return;
	}

	int x = coord_x(c, b), y = coord_y(c, b), t = board_size(b) / 2;
	int dx = board_size(b) - 1 - x; /* for SYM_DOWN */
	if (DEBUGL(6))
		fprintf(stderr, "SYMMETRY [%d,%d,%d,%d|%d=%d] update for %d,%d\n",
			symmetry->x1, symmetry->y1, symmetry->x2, symmetry->y2,
			symmetry->d, symmetry->type, x, y);
	
	switch (symmetry->type) {
		case SYM_FULL:
			if (x == t && y == t)				
				return;        /* Tengen keeps full symmetry. */
			
			/* New symmetry now? */
			if (x == y) {
				symmetry->type = SYM_DIAG_UP;
				symmetry->x1 = symmetry->y1 = 1;
				symmetry->x2 = symmetry->y2 = board_size(b) - 1;
				symmetry->d = 1;
			} else if (dx == y) {
				symmetry->type = SYM_DIAG_DOWN;
				symmetry->x1 = symmetry->y1 = 1;
				symmetry->x2 = symmetry->y2 = board_size(b) - 1;
				symmetry->d = 1;
			} else if (x == t) {
				symmetry->type = SYM_HORIZ;
				symmetry->y1 = 1;
				symmetry->y2 = board_size(b) - 1;
				symmetry->d = 0;
			} else if (y == t) {
				symmetry->type = SYM_VERT;
				symmetry->x1 = 1;
				symmetry->x2 = board_size(b) - 1;
				symmetry->d = 0;
			} else {
break_symmetry:
				symmetry->type = SYM_NONE;
				symmetry->x1 = symmetry->y1 = 1;
				symmetry->x2 = symmetry->y2 = board_size(b) - 1;
				symmetry->d = 0;
			}
			break;
		case SYM_DIAG_UP:
			if (x == y)
				return;
			goto break_symmetry;
		case SYM_DIAG_DOWN:
			if (dx == y)
				return;
			goto break_symmetry;
		case SYM_HORIZ:
			if (x == t)
				return;
			goto break_symmetry;
		case SYM_VERT:
			if (y == t)
				return;
			goto break_symmetry;
		case SYM_NONE:
			assert(0);
			break;
	}

	if (DEBUGL(6)) {
		fprintf(stderr, "NEW SYMMETRY [%d,%d,%d,%d|%d=%d]\n",
			symmetry->x1, symmetry->y1, symmetry->x2, symmetry->y2,
			symmetry->d, symmetry->type);
	}
	/* Whew. */
}


void
board_handicap_stone(struct board *board, int x, int y, FILE *f)
{
	struct move m;
	m.color = S_BLACK; m.coord = coord_xy(board, x, y);

	board_play(board, &m);

	char *str = coord2str(m.coord, board);
	if (DEBUGL(1))
		fprintf(stderr, "choosing handicap %s (%d,%d)\n", str, x, y);
	if (f) fprintf(f, "%s ", str);
	free(str);
}

void
board_handicap(struct board *board, int stones, FILE *f)
{
	int margin = 3 + (board_size(board) >= 13);
	int min = margin;
	int mid = board_size(board) / 2;
	int max = board_size(board) - 1 - margin;
	const int places[][2] = {
		{ min, min }, { max, max }, { max, min }, { min, max }, 
		{ min, mid }, { max, mid },
		{ mid, min }, { mid, max },
		{ mid, mid },
	};

	board->handicap = stones;

	if (stones == 5 || stones == 7) {
		board_handicap_stone(board, mid, mid, f);
		stones--;
	}

	int i;
	for (i = 0; i < stones; i++)
		board_handicap_stone(board, places[i][0], places[i][1], f);
}


static void
board_capturable_add(struct board *board, group_t group, coord_t lib, bool onestone)
{
	//fprintf(stderr, "group %s cap %s\n", coord2sstr(group, board), coord2sstr(lib, boarD));

#ifdef BOARD_PAT3
	int fn__i = 0;
	foreach_neighbor(board, lib, {
		board->pat3[lib] |= (group_at(board, c) == group) << (16 + 3 - fn__i);
		fn__i++;
	});
#endif

#ifdef WANT_BOARD_C
	/* Update the list of capturable groups. */
	assert(group);
	assert(board->clen < board_size2(board));
	board->c[board->clen++] = group;
#endif
}
static void
board_capturable_rm(struct board *board, group_t group, coord_t lib, bool onestone)
{
	//fprintf(stderr, "group %s nocap %s\n", coord2sstr(group, board), coord2sstr(lib, board));
#ifdef BOARD_PAT3
	int fn__i = 0;
	foreach_neighbor(board, lib, {
		board->pat3[lib] &= ~((group_at(board, c) == group) << (16 + 3 - fn__i));
		fn__i++;
	});
#endif

#ifdef WANT_BOARD_C
	/* Update the list of capturable groups. */
	for (int i = 0; i < board->clen; i++)
		if (unlikely(board->c[i] == group)) {
			board->c[i] = board->c[--board->clen];
			return;
		}
	fprintf(stderr, "rm of bad group %d\n", group_base(group));
	assert(0);
#endif
}

static void
board_group_addlib(struct board *board, group_t group, coord_t coord, struct board_undo *u)
{
	if (DEBUGL(7))
		fprintf(stderr, "Group %d[%s] %d: Adding liberty %s\n",
			group_base(group), coord2sstr(group_base(group), board),
			board_group_info(board, group).libs, coord2sstr(coord, board));

	struct group *gi = &board_group_info(board, group);
	bool onestone = group_is_onestone(board, group);
	if (gi->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0
			/* Seems extra branch just slows it down */
			if (!gi->lib[i])
				break;
#endif
			if (unlikely(gi->lib[i] == coord))
				return;
		}
		if (!u) {
			if (gi->libs == 0)
				board_capturable_add(board, group, coord, onestone);
			else if (gi->libs == 1)
				board_capturable_rm(board, group, gi->lib[0], onestone);
		}
		gi->lib[gi->libs++] = coord;
	}
}

static void
board_group_find_extra_libs(struct board *board, group_t group, struct group *gi, coord_t avoid)
{
	/* Add extra liberty from the board to our liberty list. */
	unsigned char watermark[board_size2(board) / 8];
	memset(watermark, 0, sizeof(watermark));
#define watermark_get(c)	(watermark[c >> 3] & (1 << (c & 7)))
#define watermark_set(c)	watermark[c >> 3] |= (1 << (c & 7))

	for (int i = 0; i < GROUP_KEEP_LIBS - 1; i++)
		watermark_set(gi->lib[i]);
	watermark_set(avoid);

	foreach_in_group(board, group) {
		coord_t coord2 = c;
		foreach_neighbor(board, coord2, {
			if (board_at(board, c) + watermark_get(c) != S_NONE)
				continue;
			watermark_set(c);
			gi->lib[gi->libs++] = c;
			if (unlikely(gi->libs >= GROUP_KEEP_LIBS))
				return;
		} );
	} foreach_in_group_end;
#undef watermark_get
#undef watermark_set
}

static void
board_group_rmlib(struct board *board, group_t group, coord_t coord, struct board_undo *u)
{
	if (DEBUGL(7))
		fprintf(stderr, "Group %d[%s] %d: Removing liberty %s\n",
			group_base(group), coord2sstr(group_base(group), board),
			board_group_info(board, group).libs, coord2sstr(coord, board));

	struct group *gi = &board_group_info(board, group);
	bool onestone = group_is_onestone(board, group);
	for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0
		/* Seems extra branch just slows it down */
		if (!gi->lib[i])
			break;
#endif
		if (likely(gi->lib[i] != coord))
			continue;

		coord_t lib = gi->lib[i] = gi->lib[--gi->libs];
		gi->lib[gi->libs] = 0;
		
		/* Postpone refilling lib[] until we need to. */
		assert(GROUP_REFILL_LIBS > 1);
		if (gi->libs > GROUP_REFILL_LIBS)
			return;
		if (gi->libs == GROUP_REFILL_LIBS)
			board_group_find_extra_libs(board, group, gi, coord);
		if (u) return;
		
		if (gi->libs == 1)
			board_capturable_add(board, group, gi->lib[0], onestone);
		else if (gi->libs == 0)
			board_capturable_rm(board, group, lib, onestone);
		return;
	}

	/* This is ok even if gi->libs < GROUP_KEEP_LIBS since we
	 * can call this multiple times per coord. */
	return;
}


/* This is a low-level routine that doesn't maintain consistency
 * of all the board data structures. */
static void
board_remove_stone(struct board *board, group_t group, coord_t c, struct board_undo *u)
{
	enum stone color = board_at(board, c);
	board_at(board, c) = S_NONE;
	group_at(board, c) = 0;
	if (!u)
		board_hash_update(board, c, color);

	/* Increase liberties of surrounding groups */
	coord_t coord = c;
	foreach_neighbor(board, coord, {
		dec_neighbor_count_at(board, c, color);
		group_t g = group_at(board, c);
		if (g && g != group)
			board_group_addlib(board, g, coord, u);
	});
	if (u) return;

#ifdef BOARD_PAT3
	/* board_hash_update() might have seen the freed up point as able
	 * to capture another group in atari that only after the loop
	 * above gained enough liberties. Reset pat3 again. */
	board->pat3[c] = pattern3_hash(board, c);
#endif

	if (DEBUGL(6))
		fprintf(stderr, "pushing free move [%d]: %d,%d\n", board->flen, coord_x(c, board), coord_y(c, board));
	board_addf(board, c);
}

static int profiling_noinline
board_group_capture(struct board *board, group_t group, struct board_undo *u)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_remove_stone(board, group, c, u);
		stones++;
	} foreach_in_group_end;

	struct group *gi = &board_group_info(board, group);
	assert(gi->libs == 0);
	memset(gi, 0, sizeof(*gi));

	return stones;
}


static void profiling_noinline
add_to_group(struct board *board, group_t group, coord_t prevstone, coord_t coord, struct board_undo *u)
{
	group_at(board, coord) = group;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord;

	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			board_group_addlib(board, group, c, u);
	});

	if (DEBUGL(8))
		fprintf(stderr, "add_to_group: added (%d,%d ->) %d,%d (-> %d,%d) to group %d\n",
			coord_x(prevstone, board), coord_y(prevstone, board),
			coord_x(coord, board), coord_y(coord, board),
			groupnext_at(board, coord) % board_size(board), groupnext_at(board, coord) / board_size(board),
			group_base(group));
}

static void profiling_noinline
merge_groups(struct board *board, group_t group_to, group_t group_from, struct board_undo *u)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merging groups %d -> %d\n",
			group_base(group_from), group_base(group_to));
	struct group *gi_from = &board_group_info(board, group_from);
	struct group *gi_to = &board_group_info(board, group_to);
	bool onestone_from = group_is_onestone(board, group_from);
	bool onestone_to = group_is_onestone(board, group_to);

	if (!u)
		/* We do this early before the group info is rewritten. */
		if (gi_from->libs == 1)
			board_capturable_rm(board, group_from, gi_from->lib[0], onestone_from);

	if (DEBUGL(7))
		fprintf(stderr,"---- (froml %d, tol %d)\n", gi_from->libs, gi_to->libs);

	if (gi_to->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < gi_from->libs; i++) {
			for (int j = 0; j < gi_to->libs; j++)
				if (gi_to->lib[j] == gi_from->lib[i])
					goto next_from_lib;
			if (!u) {
				if (gi_to->libs == 0)
					board_capturable_add(board, group_to, gi_from->lib[i], onestone_to);
				else if (gi_to->libs == 1)
					board_capturable_rm(board, group_to, gi_to->lib[0], onestone_to);
			}
			gi_to->lib[gi_to->libs++] = gi_from->lib[i];
			if (gi_to->libs >= GROUP_KEEP_LIBS)
				break;
next_from_lib:;
		}
	}

	if (!u && gi_to->libs == 1) {
		coord_t lib = board_group_info(board, group_to).lib[0];
#ifdef BOARD_PAT3
		if (gi_from->libs == 1) {
			/* We removed group_from from capturable groups,
			 * therefore switching the atari flag off.
			 * We need to set it again since group_to is also
			 * capturable. */
			int fn__i = 0;
			foreach_neighbor(board, lib, {
				board->pat3[lib] |= (group_at(board, c) == group_from) << (16 + 3 - fn__i);
				fn__i++;
			});
		}
#endif
	}

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;

	if (u) u->merged[++u->nmerged_tmp].last = last_in_group;
	groupnext_at(board, last_in_group) = groupnext_at(board, group_base(group_to));
	groupnext_at(board, group_base(group_to)) = group_base(group_from);
	memset(gi_from, 0, sizeof(struct group));

	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merged group: %d\n",
			group_base(group_to));
}

static group_t profiling_noinline
new_group(struct board *board, coord_t coord, struct board_undo *u)
{
	group_t group = coord;
	struct group *gi = &board_group_info(board, group);
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			/* board_group_addlib is ridiculously expensive for us */
#if GROUP_KEEP_LIBS < 4
			if (gi->libs < GROUP_KEEP_LIBS)
#endif
			gi->lib[gi->libs++] = c;
	});

	group_at(board, coord) = group;
	groupnext_at(board, coord) = 0;

	if (!u)
		if (gi->libs == 1)
			board_capturable_add(board, group, gi->lib[0], true);

	if (DEBUGL(8))
		fprintf(stderr, "new_group: added %d,%d to group %d\n",
			coord_x(coord, board), coord_y(coord, board),
			group_base(group));

	return group;
}

static inline void
undo_save_merge(struct board *b, struct board_undo *u, group_t g, coord_t c)
{
	if (g == u->merged[0].group || g == u->merged[1].group || 
	    g == u->merged[2].group || g == u->merged[3].group)
		return;
	
	int i = u->nmerged++;
	if (!i) u->inserted = c;
	u->merged[i].group = g;
	u->merged[i].last = 0;   // can remove
	u->merged[i].info = board_group_info(b, g);
}

static inline void
undo_save_enemy(struct board *b, struct board_undo *u, group_t g)
{
	if (g == u->enemies[0].group || g == u->enemies[1].group ||
	    g == u->enemies[2].group || g == u->enemies[3].group)
		return;
	
	int i = u->nenemies++;
	u->enemies[i].group = g;
	u->enemies[i].info = board_group_info(b, g);
	u->enemies[i].stones = NULL;
	
	if (board_group_info(b, g).libs <= 1) { // Will be captured
		coord_t *stones = u->enemies[i].stones = u->captures_end;
		int j = 0;
		foreach_in_group(b, g) {
			stones[j++] = c;
		} foreach_in_group_end;
		u->ncaptures += j;
		stones[j++] = 0;
		u->captures_end = &stones[j];
	}
}

static void
undo_save_group_info(struct board *b, coord_t coord, enum stone color, struct board_undo *u)
{
	u->next_at = groupnext_at(b, coord);

	foreach_neighbor(b, coord, {			
		group_t g = group_at(b, c);
	
		if (board_at(b, c) == color)
			undo_save_merge(b, u, g, c);
		else if (board_at(b, c) == stone_other(color)) 
			undo_save_enemy(b, u, g);
	});
}		

static void
undo_save_suicide(struct board *b, coord_t coord, enum stone color, struct board_undo *u)
{
	foreach_neighbor(b, coord, {
		if (board_at(b, c) == color) {
			// Handle suicide as a capture ...
			undo_save_enemy(b, u, group_at(b, c));
			return;
		}
	});
	assert(0);
}

static inline group_t
play_one_neighbor(struct board *board,
		  coord_t coord, enum stone color, enum stone other_color,
		  coord_t c, group_t group, struct board_undo *u)
{
	enum stone ncolor = board_at(board, c);
	group_t ngroup = group_at(board, c);

	inc_neighbor_count_at(board, c, color);

	if (!ngroup)
		return group;

	board_group_rmlib(board, ngroup, coord, u);
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: reducing libs for group %d (%d:%d,%d)\n",
			group_base(ngroup), ncolor, color, other_color);

	if (ncolor == color && ngroup != group) {
		if (!group) {
			group = ngroup;
			add_to_group(board, group, c, coord, u);
		} else
			merge_groups(board, group, ngroup, u);
	} else if (ncolor == other_color) {
		if (DEBUGL(8)) {
			struct group *gi = &board_group_info(board, ngroup);
			fprintf(stderr, "testing captured group %d[%s]: ", group_base(ngroup), coord2sstr(group_base(ngroup), board));
			for (int i = 0; i < GROUP_KEEP_LIBS; i++)
				fprintf(stderr, "%s ", coord2sstr(gi->lib[i], board));
			fprintf(stderr, "\n");
		}
		if (unlikely(board_group_captured(board, ngroup)))
			board_group_capture(board, ngroup, u);
	}
	return group;
}

/* We played on a place with at least one liberty. We will become a member of
 * some group for sure. */
static group_t profiling_noinline
board_play_outside(struct board *board, struct move *m, int f, struct board_undo *u)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	group_t group = 0;

	if (u)  
		undo_save_group_info(board, coord, color, u);
	else {
		board_rmf(board, f);
		if (DEBUGL(6))
			fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);
	}
	foreach_neighbor(board, coord, {
			group = play_one_neighbor(board, coord, color, other_color, c, group, u);
	});

	board_at(board, coord) = color;
	if (unlikely(!group))
		group = new_group(board, coord, u);

	if (!u) {
		board->last_move4 = board->last_move3;
		board->last_move3 = board->last_move2;
	}
	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	if (!u) {
		board_hash_update(board, coord, color);
		board_symmetry_update(board, &board->symmetry, coord);
	}
	struct move ko = { pass, S_NONE };
	board->ko = ko;

	return group;
}

/* We played in an eye-like shape. Either we capture at least one of the eye
 * sides in the process of playing, or return -1. */
static int profiling_noinline
board_play_in_eye(struct board *board, struct move *m, int f, struct board_undo *u)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	/* Check ko: Capture at a position of ko capture one move ago */
	if (unlikely(color == board->ko.color && coord == board->ko.coord)) {
		if (DEBUGL(5))
			fprintf(stderr, "board_check: ko at %d,%d color %d\n", coord_x(coord, board), coord_y(coord, board), color);
		return -1;
	} else if (DEBUGL(6))
		fprintf(stderr, "board_check: no ko at %d,%d,%d - ko is %d,%d,%d\n",
			color, coord_x(coord, board), coord_y(coord, board),
			board->ko.color, coord_x(board->ko.coord, board), coord_y(board->ko.coord, board));

	struct move ko = { pass, S_NONE };

	int captured_groups = 0;

	foreach_neighbor(board, coord, {
		group_t g = group_at(board, c);
		if (DEBUGL(7))
			fprintf(stderr, "board_check: group %d has %d libs\n",
				g, board_group_info(board, g).libs);
		captured_groups += (board_group_info(board, g).libs == 1);
	});

	if (likely(captured_groups == 0)) {
		if (DEBUGL(5)) {
			if (DEBUGL(6))  board_print(board, stderr);
			fprintf(stderr, "board_check: one-stone suicide\n");
		}
		return -1;
	}

	if (!u) {
		board_rmf(board, f);
		if (DEBUGL(6))
			fprintf(stderr, "popping free move [%d->%d]: %d\n", board->flen, f, board->f[f]);
	}
	else
		undo_save_group_info(board, coord, color, u);

	int ko_caps = 0;
	coord_t cap_at = pass;
	foreach_neighbor(board, coord, {
		inc_neighbor_count_at(board, c, color);

		group_t group = group_at(board, c);
		if (!group)
			continue;

		board_group_rmlib(board, group, coord, u);
		if (DEBUGL(7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d\n",
				group_base(group));

		if (board_group_captured(board, group)) {
			ko_caps += board_group_capture(board, group, u);
			cap_at = c;
		}
	});
	if (ko_caps == 1) {
		ko.color = stone_other(color);
		ko.coord = cap_at; // unique
		board->last_ko = ko;
		board->last_ko_age = board->moves + 1;  /* == board->moves really, board->moves++ done after */
		if (DEBUGL(5))
			fprintf(stderr, "guarding ko at %d,%s\n", ko.color, coord2sstr(ko.coord, board));
	}

	board_at(board, coord) = color;
	group_t group = new_group(board, coord, u);

	if (!u) {
		board->last_move4 = board->last_move3;
		board->last_move3 = board->last_move2;
	}
	board->last_move2 = board->last_move;
	board->last_move = *m;
	board->moves++;
	if (!u) {
		board_hash_update(board, coord, color);
		board_hash_commit(board);
		board_symmetry_update(board, &board->symmetry, coord);
	}
	board->ko = ko;

	return !!group;
}

static int __attribute__((flatten))
board_play_f(struct board *board, struct move *m, int f, struct board_undo *u)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play(%s): ---- Playing %d,%d\n", coord2sstr(m->coord, board), coord_x(m->coord, board), coord_y(m->coord, board));
	if (likely(!board_is_eyelike(board, m->coord, stone_other(m->color)))) {
		/* NOT playing in an eye. Thus this move has to succeed. (This
		 * is thanks to New Zealand rules. Otherwise, multi-stone
		 * suicide might fail.) */
		group_t group = board_play_outside(board, m, f, u);
		if (unlikely(board_group_captured(board, group))) {
			if (u) undo_save_suicide(board, m->coord, m->color, u);
			board_group_capture(board, group, u);
		}
		if (!u)
			board_hash_commit(board);
		return 0;
	} else
		return board_play_in_eye(board, m, f, u);
}

static void
undo_init(struct board *b, struct move *m, struct board_undo *u)
{
	// Paranoid uninitialized mem test
	// memset(u, 0xff, sizeof(*u));
	
	u->last_move2 = b->last_move2;
	u->ko = b->ko;
	u->last_ko = b->last_ko;
	u->last_ko_age = b->last_ko_age;
	u->captures_end = &u->captures[0];
	u->ncaptures = 0;
	
	u->nmerged = u->nmerged_tmp = u->nenemies = 0;
	for (int i = 0; i < 4; i++)
		u->merged[i].group = u->enemies[i].group = 0;
}

static int
board_play_(struct board *board, struct move *m, struct board_undo *u)
{
#ifdef BOARD_UNDO_CHECKS
	assert(u || !board->quicked);
#endif

	if (u) undo_init(board, m, u);
	
	if (unlikely(is_pass(m->coord) || is_resign(m->coord))) {
		if (is_pass(m->coord) && board->rules == RULES_SIMING) {
			/* On pass, the player gives a pass stone
			 * to the opponent. */
			board->captures[stone_other(m->color)]++;
		}
		struct move nomove = { pass, S_NONE };
		board->ko = nomove;
		if (!u) { 
			board->last_move4 = board->last_move3;
			board->last_move3 = board->last_move2;
		}
		board->last_move2 = board->last_move;
		board->last_move = *m;
		board->moves++;
		return 0;
	}

	if (unlikely(board_at(board, m->coord) != S_NONE)) {
		if (DEBUGL(7)) fprintf(stderr, "board_check: stone exists\n");
		return -1;
	}

	int f = (u ? -1 : board->fmap[m->coord]);
	return board_play_f(board, m, f, u);
}

int
board_play(struct board *board, struct move *m)
{
	return board_play_(board, m, NULL);
}

int
board_quick_play(struct board *board, struct move *m, struct board_undo *u)
{
	int r = board_play_(board, m, u);
#ifdef BOARD_UNDO_CHECKS
	if (r >= 0)
		board->quicked++;
#endif
	return r;
}

static inline void
undo_merge(struct board *b, struct board_undo *u, struct move *m)
{
	coord_t coord = m->coord;
	group_t group = group_at(b, coord);
	struct undo_merge *merged = u->merged;
	
	// Others groups, in reverse order ...
	for (int i = u->nmerged - 1; i > 0; i--) {
		group_t old_group = merged[i].group;
			
		board_group_info(b, old_group) = merged[i].info;
			
		groupnext_at(b, group_base(group)) = groupnext_at(b, merged[i].last);
		groupnext_at(b, merged[i].last) = 0;

#if 0
		printf("merged_group[%i]:   (last: %s)", i, coord2sstr(merged[i].last, b));
		foreach_in_group(b, old_group) {
			printf("%s ", coord2sstr(c, b));
		} foreach_in_group_end;
		printf("\n");
#endif
			
		foreach_in_group(b, old_group) {
			group_at(b, c) = old_group;
		} foreach_in_group_end;
	}

	// Restore first group
	groupnext_at(b, u->inserted) = groupnext_at(b, coord);
	board_group_info(b, merged[0].group) = merged[0].info;

#if 0
	printf("merged_group[0]: ");
	foreach_in_group(b, merged[0].group) {
		printf("%s ", coord2sstr(c, b));
	} foreach_in_group_end;
	printf("\n");
#endif
}


static inline void
restore_enemies(struct board *b, struct board_undo *u, struct move *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);
	
	struct undo_enemy *enemy = u->enemies;
	for (int i = 0; i < u->nenemies; i++) {
		group_t old_group = enemy[i].group;
			
		board_group_info(b, old_group) = enemy[i].info;
			
		coord_t *stones = enemy[i].stones;
		if (!stones)  continue;

		for (int j = 0; stones[j]; j++) {
			board_at(b, stones[j]) = other_color;
			group_at(b, stones[j]) = old_group;
			groupnext_at(b, stones[j]) = stones[j + 1];

			foreach_neighbor(b, stones[j], {
				inc_neighbor_count_at(b, c, other_color);
			});

			// Update liberties of neighboring groups
			foreach_neighbor(b, stones[j], {
					if (board_at(b, c) != color)
						continue;
					group_t g = group_at(b, c);
					if (g == u->merged[0].group || g == u->merged[1].group || g == u->merged[2].group || g == u->merged[3].group)
						continue;
					board_group_rmlib(b, g, stones[j], u);
				});
		}
	}
}

static void
board_undo_stone(struct board *b, struct board_undo *u, struct move *m)
{	
	coord_t coord = m->coord;
	enum stone color = m->color;
	/* - update groups
	 * - put captures back
	 */
	
	//printf("nmerged: %i\n", u->nmerged);
	
	// Restore merged groups
	if (u->nmerged)
		undo_merge(b, u, m);
	else			// Single stone group undo
		memset(&board_group_info(b, group_at(b, coord)), 0, sizeof(struct group));
	
	board_at(b, coord) = S_NONE;
	group_at(b, coord) = 0;
	groupnext_at(b, coord) = u->next_at;
	
	foreach_neighbor(b, coord, {
			dec_neighbor_count_at(b, c, color);
	});

	// Restore enemy groups
	if (u->nenemies) {
		b->captures[color] -= u->ncaptures;
		restore_enemies(b, u, m);
	}
}

static inline void
restore_suicide(struct board *b, struct board_undo *u, struct move *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);
	
	struct undo_enemy *enemy = u->enemies;
	for (int i = 0; i < u->nenemies; i++) {
		group_t old_group = enemy[i].group;
			
		board_group_info(b, old_group) = enemy[i].info;
			
		coord_t *stones = enemy[i].stones;
		if (!stones)  continue;

		for (int j = 0; stones[j]; j++) {
			board_at(b, stones[j]) = other_color;
			group_at(b, stones[j]) = old_group;
			groupnext_at(b, stones[j]) = stones[j + 1];

			foreach_neighbor(b, stones[j], {
				inc_neighbor_count_at(b, c, other_color);
			});

			// Update liberties of neighboring groups
			foreach_neighbor(b, stones[j], {
					if (board_at(b, c) != color)
						continue;
					group_t g = group_at(b, c);
					if (g == u->enemies[0].group || g == u->enemies[1].group || 
					    g == u->enemies[2].group || g == u->enemies[3].group)
						continue;
					board_group_rmlib(b, g, stones[j], u);
				});
		}
	}
}


static void
board_undo_suicide(struct board *b, struct board_undo *u, struct move *m)
{	
	coord_t coord = m->coord;
	enum stone other_color = stone_other(m->color);
	
	// Pretend it's capture ...
	struct move m2 = { .coord = m->coord, .color = other_color };
	b->captures[other_color] -= u->ncaptures;
	
	restore_suicide(b, u, &m2);

	undo_merge(b, u, m);

	board_at(b, coord) = S_NONE;
	group_at(b, coord) = 0;
	groupnext_at(b, coord) = u->next_at;

	foreach_neighbor(b, coord, {
		dec_neighbor_count_at(b, c, m->color);
	});

}


void
board_quick_undo(struct board *b, struct move *m, struct board_undo *u)
{
#ifdef BOARD_UNDO_CHECKS
	b->quicked--;
#endif
	
	b->last_move = b->last_move2;
	b->last_move2 = u->last_move2;
	b->ko = u->ko;
	b->last_ko = u->last_ko;
	b->last_ko_age = u->last_ko_age;
	
	if (unlikely(is_pass(m->coord) || is_resign(m->coord))) 
		return;

	b->moves--;

	if (likely(board_at(b, m->coord) == m->color))
		board_undo_stone(b, u, m);
	else if (board_at(b, m->coord) == S_NONE)
		board_undo_suicide(b, u, m);
	else
		assert(0);	/* Anything else doesn't make sense */
}


/* Undo, supported only for pass moves. This form of undo is required by KGS
 * to settle disputes on dead groups. See also fast_board_undo() */
int board_undo(struct board *board)
{
	if (!is_pass(board->last_move.coord))
		return -1;
	if (board->rules == RULES_SIMING) {
		/* Return pass stone to the passing player. */
		board->captures[stone_other(board->last_move.color)]--;
	}
	board->last_move = board->last_move2;
	board->last_move2 = board->last_move3;
	board->last_move3 = board->last_move4;
	board->moves--;
	if (board->last_ko_age == board->moves)
		board->ko = board->last_ko;
	return 0;
}

bool
board_permit(struct board *b, struct move *m, void *data)
{
	if (unlikely(board_is_one_point_eye(b, m->coord, m->color)) /* bad idea to play into one, usually */
	    || !board_is_valid_move(b, m))
		return false;
	return true;
}

static inline bool
board_try_random_move(struct board *b, enum stone color, coord_t *coord, int f, ppr_permit permit, void *permit_data)
{
	*coord = b->f[f];
	struct move m = { *coord, color };
	if (DEBUGL(6))
		fprintf(stderr, "trying random move %d: %d,%d %s %d\n", f, coord_x(*coord, b), coord_y(*coord, b), coord2sstr(*coord, b), board_is_valid_move(b, &m));
	permit = (permit ? permit : board_permit);
	if (!permit(b, &m, permit_data))
		return false;
	if (m.coord == *coord)
		return likely(board_play_f(b, &m, f, NULL) >= 0);
	*coord = m.coord; // permit modified the coordinate
	return likely(board_play(b, &m) >= 0);
}

void
board_play_random(struct board *b, enum stone color, coord_t *coord, ppr_permit permit, void *permit_data)
{
	if (unlikely(b->flen == 0))
		goto play_pass;

	int base = fast_random(b->flen), f;
	for (f = base; f < b->flen; f++)
		if (board_try_random_move(b, color, coord, f, permit, permit_data))
			return;
	for (f = 0; f < base; f++)
		if (board_try_random_move(b, color, coord, f, permit, permit_data))
			return;

play_pass:
	*coord = pass;
	struct move m = { pass, color };
	board_play(b, &m);
}


bool
board_is_false_eyelike(struct board *board, coord_t coord, enum stone eye_color)
{
	enum stone color_diag_libs[S_MAX] = {0, 0, 0, 0};

	/* XXX: We attempt false eye detection but we will yield false
	 * positives in case of http://senseis.xmp.net/?TwoHeadedDragon :-( */

	foreach_diag_neighbor(board, coord) {
		color_diag_libs[(enum stone) board_at(board, c)]++;
	} foreach_diag_neighbor_end;
	/* For false eye, we need two enemy stones diagonally in the
	 * middle of the board, or just one enemy stone at the edge
	 * or in the corner. */
	color_diag_libs[stone_other(eye_color)] += !!color_diag_libs[S_OFFBOARD];
	return color_diag_libs[stone_other(eye_color)] >= 2;
}

bool
board_is_one_point_eye(struct board *board, coord_t coord, enum stone eye_color)
{
	return board_is_eyelike(board, coord, eye_color)
		&& !board_is_false_eyelike(board, coord, eye_color);
}

enum stone
board_get_one_point_eye(struct board *board, coord_t coord)
{
	if (board_is_one_point_eye(board, coord, S_WHITE))
		return S_WHITE;
	else if (board_is_one_point_eye(board, coord, S_BLACK))
		return S_BLACK;
	else
		return S_NONE;
}


floating_t
board_fast_score(struct board *board)
{
	int scores[S_MAX];
	memset(scores, 0, sizeof(scores));

	foreach_point(board) {
		enum stone color = board_at(board, c);
		if (color == S_NONE && board->rules != RULES_STONES_ONLY)
			color = board_get_one_point_eye(board, c);
		scores[color]++;
		// fprintf(stderr, "%d, %d ++%d = %d\n", coord_x(c, board), coord_y(c, board), color, scores[color]);
	} foreach_point_end;

	int handi_comp = board_score_handicap_compensation(board);
	return board->komi + handi_comp + scores[S_WHITE] - scores[S_BLACK];
}

/* Owner map: 0: undecided; 1: black; 2: white; 3: dame */

/* One flood-fill iteration; returns true if next iteration
 * is required. */
static bool
board_tromp_taylor_iter(struct board *board, int *ownermap)
{
	bool needs_update = false;
	foreach_free_point(board) {
		/* Ignore occupied and already-dame positions. */
		assert(board_at(board, c) == S_NONE);
		if (board->rules == RULES_STONES_ONLY)
		    ownermap[c] = 3;
		if (ownermap[c] == 3)
			continue;
		/* Count neighbors. */
		int nei[4] = {0};
		foreach_neighbor(board, c, {
			nei[ownermap[c]]++;
		});
		/* If we have neighbors of both colors, or dame,
		 * we are dame too. */
		if ((nei[1] && nei[2]) || nei[3]) {
			ownermap[c] = 3;
			/* Speed up the propagation. */
			foreach_neighbor(board, c, {
				if (board_at(board, c) == S_NONE)
					ownermap[c] = 3;
			});
			needs_update = true;
			continue;
		}
		/* If we have neighbors of one color, we are owned
		 * by that color, too. */
		if (!ownermap[c] && (nei[1] || nei[2])) {
			int newowner = nei[1] ? 1 : 2;
			ownermap[c] = newowner;
			/* Speed up the propagation. */
			foreach_neighbor(board, c, {
				if (board_at(board, c) == S_NONE && !ownermap[c])
					ownermap[c] = newowner;
			});
			needs_update = true;
			continue;
		}
	} foreach_free_point_end;
	return needs_update;
}

int
board_score_handicap_compensation(struct board *b)
{
	switch (b->rules) {		
		case RULES_SIMING:    return 0;

		/* Usually this makes territory and area scoring the same.
		 * See handicap go section of:
		 * https://senseis.xmp.net/?TerritoryScoringVersusAreaScoring */
		case RULES_JAPANESE:
		case RULES_AGA:	      return (b->handicap ? b->handicap - 1 : 0);

		/* RULES_CHINESE etc */
		default:              return  b->handicap;
	}
	
	assert(0);  /* not reached */
}

void
board_print_official_ownermap(struct board *b, int *final_ownermap)
{
	for (int y = board_size(b) - 2; y >= 1; y--) {
		for (int x = 1; x < board_size(b) - 1; x++) {
			coord_t c = coord_xy(b, x, y);
			char *chars = ".XO:";
			fprintf(stderr, "%c ", chars[final_ownermap[c]]);
		}
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");
}

/* Official score after removing dead groups and Tromp-Taylor counting.
 * Number of dames is saved in @dames, final ownermap in @ownermap. */
floating_t
board_official_score_details(struct board *board, struct move_queue *dead, int *dames, int *ownermap)
{
	/* A point P, not colored C, is said to reach C, if there is a path of
	 * (vertically or horizontally) adjacent points of P's color from P to
	 * a point of color C.
	 *
	 * A player's score is the number of points of her color, plus the
	 * number of empty points that reach only her color. */

	int s[4] = {0};
	const int o[4] = {0, 1, 2, 0};
	foreach_point(board) {
		ownermap[c] = o[board_at(board, c)];
		s[board_at(board, c)]++;
	} foreach_point_end;

	if (dead) {
		/* Process dead groups. */
		for (unsigned int i = 0; i < dead->moves; i++) {
			foreach_in_group(board, dead->move[i]) {
				enum stone color = board_at(board, c);
				ownermap[c] = o[stone_other(color)];
				s[color]--; s[stone_other(color)]++;
			} foreach_in_group_end;
		}
	}

	/* We need to special-case empty board. */
	if (!s[S_BLACK] && !s[S_WHITE])
		return board->komi;

	while (board_tromp_taylor_iter(board, ownermap))
		/* Flood-fill... */;

	int scores[S_MAX] = { 0, };

	foreach_point(board) {
		assert(board_at(board, c) == S_OFFBOARD || ownermap[c] != 0);
		scores[ownermap[c]]++;
	} foreach_point_end;
	*dames = scores[3];

	int handi_comp = board_score_handicap_compensation(board);
	return board->komi + handi_comp + scores[S_WHITE] - scores[S_BLACK];
}

floating_t
board_official_score(struct board *b, struct move_queue *dead)
{
	int dame;
	int ownermap[board_size2(b)];
	return board_official_score_details(b, dead, &dame, ownermap);
}

bool
board_set_rules(struct board *board, char *name)
{
	if (!strcasecmp(name, "japanese"))
		board->rules = RULES_JAPANESE;
	else if (!strcasecmp(name, "chinese"))
		board->rules = RULES_CHINESE;
	else if (!strcasecmp(name, "aga"))
		board->rules = RULES_AGA;
	else if (!strcasecmp(name, "new_zealand"))
		board->rules = RULES_NEW_ZEALAND;
	else if (!strcasecmp(name, "siming") || !strcasecmp(name, "simplified_ing"))
		board->rules = RULES_SIMING;
	else
		return false;
	return true;
}

const char*
rules2str(enum go_ruleset rules)
{
	switch (rules) {
		case RULES_CHINESE:     return "chinese";
		case RULES_AGA:         return "aga";
		case RULES_NEW_ZEALAND: return "new_zealand";
		case RULES_JAPANESE:    return "japanese";
		case RULES_STONES_ONLY: return "stones_only";
		case RULES_SIMING:      return "simplified_ing";
		default:                die("invalid rules: %i\n", rules);
	}
	return NULL;
}
