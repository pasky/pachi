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
#include "ownermap.h"
#include "dcnn.h"

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

static int  board_play_f(board_t *board, move_t *m, int f);
static void board_addf(board_t *b, coord_t c);
static void board_rmf(board_t *b, int f);


static void
board_setup(board_t *b)
{
	assert(BOARD_LAST_N >= 4);

	memset(b, 0, sizeof(*b));

	b->rules = RULES_CHINESE;
	move_t m = { pass, S_NONE };
	for (int i = 0; i < BOARD_LAST_N; i++)
		last_moven(b, i) = m;
	b->last_ko = b->ko = m;
}

board_t *
board_new(int size, char *fbookfile)
{
	board_t *b = malloc2(board_t);
	board_setup(b);
	b->fbookfile = fbookfile;
	b->rsize = size;
	board_clear(b);	
	return b;
}

void
board_delete(board_t **b)
{
	board_done(*b);
	free(*b);
	*b = NULL;
}

int
board_cmp(board_t *b1, board_t *b2)
{
	return memcmp(b1, b2, sizeof(board_t));
}

void
board_copy(board_t *b2, board_t *b1)
{
	memcpy(b2, b1, sizeof(board_t));

	// XXX: Special semantics.
	b2->fbook = NULL;
	b2->ps = NULL;
}

void
board_done(board_t *board)
{
	if (board->fbook) fbook_done(board->fbook);
	if (board->ps) free(board->ps);
}

void
board_resize(board_t *board, int size)
{
#ifdef BOARD_SIZE
	assert(board_rsize(board) == size);
#endif
	assert(size <= BOARD_MAX_SIZE);
	board->rsize = size;
}

board_statics_t board_statics = { 0, };

static void
board_statics_init(board_t *board)
{
	int size = board_rsize(board);
	int stride = size + 2;
	board_statics_t *bs = &board_statics;
	if (bs->rsize == size)
		return;
	
	memset(bs, 0, sizeof(*bs));
	bs->rsize = size;
	bs->stride = stride;
	bs->max_coords = stride * stride;

	bs->bits2 = 1;
	while ((1 << bs->bits2) < bs->max_coords)  bs->bits2++;
	
	/* Setup neighborhood iterators */
	bs->nei8[0] = -stride - 1; // (-1,-1)
	bs->nei8[1] = 1;
	bs->nei8[2] = 1;
	bs->nei8[3] = stride - 2; // (-1,0)
	bs->nei8[4] = 2;
	bs->nei8[5] = stride - 2; // (-1,1)
	bs->nei8[6] = 1;
	bs->nei8[7] = 1;
	
	bs->dnei[0] = -stride - 1;
	bs->dnei[1] = 2;
	bs->dnei[2] = stride*2 - 2;
	bs->dnei[3] = 2;

	/* Set up coordinate cache */
	foreach_point(board) {
		bs->coord[c][0] = c % stride;
		bs->coord[c][1] = c / stride;
	} foreach_point_end;

	/* Initialize zobrist hashtable. */
	/* We will need these to be stable across Pachi runs for certain kinds
	 * of pattern matching, thus we do not use fast_random() for this. */
	hash_t hseed = 0x3121110101112131;
#ifdef BOARD_HASH_COMPAT
        /* Until <board_cleanup> board->h was treated as h[BOARD_MAX_COORDS][2] here
	 * and h[2][BOARD_MAX_COORDS] in hash_at(). Preserve quirk to get same hashes. */
        hash_t (*hash)[2] = (hash_t (*)[2])bs->h;
        for (coord_t c = 0; c < BOARD_MAX_COORDS; c++) {  /* Don't foreach_point(), need all 21x21 */
		for (int color = 0; color <= 1; color++) {
			hash[c][color] = (hseed *= 16807);
			if (!hash[c][color])  hash[c][color] = 1;
		}
	}
#else
	for (coord_t c = 0; c < BOARD_MAX_COORDS; c++) {  /* Don't foreach_point(), need all 21x21 */
		for (int color = S_BLACK; color <= S_WHITE; color++) {
			hash_at(c, color) = (hseed *= 16807);
			if (!hash_at(c, color))  hash_at(c, color) = 1;
		}
	}
#endif

	/* Sanity check ... */
	foreach_point(board) {
		assert(hash_at(c, S_BLACK) != 0);
		assert(hash_at(c, S_WHITE) != 0);
	} foreach_point_end;
}

static void
board_init_data(board_t *board)
{
	int size = board_rsize(board);
	int stride = board_stride(board);

	board_setup(board);
	board_resize(board, size);

	/* Draw the offboard margin */
	int top_row = board_max_coords(board) - stride;
	int i;
	for (i = 0; i < stride; i++)
		board->b[i] = board->b[top_row + i] = S_OFFBOARD;
	for (i = 0; i <= top_row; i += stride)
		board->b[i] = board->b[i + stride - 1] = S_OFFBOARD;

	foreach_point(board) {
		coord_t coord = c;
		if (board_at(board, coord) == S_OFFBOARD)
			continue;
		foreach_neighbor(board, c, {
			inc_neighbor_count_at(board, coord, board_at(board, c));
		} );
	} foreach_point_end;

	/* All positions are free! Except the margin. */
	foreach_point(board) {
		if (board_at(board, c) == S_NONE)
			board_addf(board, c);
	} foreach_point_end;
	assert(board->flen == size * size);

#ifdef BOARD_PAT3
	/* Initialize 3x3 pattern codes. */
	foreach_point(board) {
		if (board_at(board, c) == S_NONE)
			board->pat3[c] = pattern3_hash(board, c);
	} foreach_point_end;
#endif
}

void
board_clear(board_t *board)
{
	int size = board_rsize(board);
	floating_t komi = board->komi;
	char *fbookfile = board->fbookfile;
	enum rules rules = board->rules;

	board_done(board);

	board_statics_init(board);
	static board_t bcache[BOARD_MAX_SIZE + 2];
	assert(size > 1 && size <= BOARD_MAX_SIZE);
	if (bcache[size - 1].rsize == size)
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
board_print_top(board_t *board, strbuf_t *buf, int c)
{
	int size = board_rsize(board);
	for (int i = 0; i < c; i++) {
		char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
		sbprintf(buf, "      ");
		for (int x = 1; x <= size; x++)
			sbprintf(buf, "%c ", asdf[x - 1]);
		sbprintf(buf, " ");
	}
	sbprintf(buf, "\n");
	for (int i = 0; i < c; i++) {
		sbprintf(buf, "    +-");
		for (int x = 1; x <= size; x++)
			sbprintf(buf, "--");
		sbprintf(buf, "+");
	}
	sbprintf(buf, "\n");
}

static void
board_print_bottom(board_t *board, strbuf_t *buf, int c)
{
	int size = board_rsize(board);
	for (int i = 0; i < c; i++) {
		sbprintf(buf, "    +-");
		for (int x = 1; x <= size; x++)
			sbprintf(buf, "--");
		sbprintf(buf, "+");
	}
	sbprintf(buf, "\n");
}

static void
board_print_row(board_t *board, int y, strbuf_t *buf, board_cprint cprint, void *data)
{
	int size = board_rsize(board);
	sbprintf(buf, " %2d | ", y);
	for (int x = 1; x <= size; x++)
		if (coord_x(last_move(board).coord) == x && coord_y(last_move(board).coord) == y)
			sbprintf(buf, "%c)", stone2char(board_atxy(board, x, y)));
		else
			sbprintf(buf, "%c ", stone2char(board_atxy(board, x, y)));
	sbprintf(buf, "|");
	if (cprint) {
		sbprintf(buf, " %2d | ", y);
		for (int x = 1; x <= size; x++)
			cprint(board, coord_xy(x, y), buf, data);
		sbprintf(buf, "|");
	}
	sbprintf(buf, "\n");
}

void
board_print_custom(board_t *board, FILE *f, board_cprint cprint, void *data)
{
	char buffer[10240];
	strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	int size = board_rsize(board);

	sbprintf(buf, "Move: % 3d  Komi: %2.1f  Handicap: %d  Captures B: %d W: %d  ",
		 board->moves, board->komi, board->handicap,
		 board->captures[S_BLACK], board->captures[S_WHITE]);	
	
	if (cprint) /* handler can add things to header when called with pass */
		cprint(board, pass, buf, data);
	sbprintf(buf, "\n");
	
	board_print_top(board, buf, 1 + !!cprint);
	for (int y = size; y >= 1; y--)
		board_print_row(board, y, buf, cprint, data);
	board_print_bottom(board, buf, 1 + !!cprint);
	fprintf(f, "%s\n", buf->str);
}

static void
board_hprint_row(board_t *board, int y, strbuf_t *buf, board_print_handler handler, void *data)
{
	int size = board_rsize(board);
        sbprintf(buf, " %2d | ", y);
	for (int x = 1; x <= size; x++) {
                char *stone_str = handler(board, coord_xy(x, y), data);
                if (coord_x(last_move(board).coord) == x && coord_y(last_move(board).coord) == y)
                        sbprintf(buf, "%s)", stone_str);
                else
                        sbprintf(buf, "%s ", stone_str);
        }
        sbprintf(buf, "|\n");
}

void
board_hprint(board_t *board, FILE *f, board_print_handler handler, void *data)
{
        char buffer[10240];
	strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));
	int size = board_rsize(board);
	
        sbprintf(buf, "Move: % 3d  Komi: %2.1f  Handicap: %d  Captures B: %d W: %d\n",
		 board->moves, board->komi, board->handicap,
		 board->captures[S_BLACK], board->captures[S_WHITE]);
	
	board_print_top(board, buf, 1);
	for (int y = size; y >= 1; y--)
                board_hprint_row(board, y, buf, handler, data);
        board_print_bottom(board, buf, 1);
        fprintf(f, "%s\n", buf->str);
}

static void
cprint_group(board_t *board, coord_t c, strbuf_t *buf, void *data)
{
	sbprintf(buf, "%d ", group_base(group_at(board, c)));
}

void
board_print(board_t *board, FILE *f)
{
	board_print_custom(board, f, DEBUGL(6) ? cprint_group : NULL, NULL);
}

static char*
print_target_move_handler(board_t *b, coord_t c, void *data)
{	
	static char buf[32];
	coord_t target_move = (coord_t)(intptr_t)data;

	if (c == target_move)	sprintf(buf, "\e[40;33;1m*\e[0m");
	else			sprintf(buf, "%c", stone2char(board_at(b, c)));
	return buf;
}

void
board_print_target_move(board_t *b, FILE *f, coord_t target_move)
{
	assert(!is_pass(target_move));
	assert(board_at(b, target_move) == S_NONE);
	board_hprint(b, f, print_target_move_handler, (void*)(intptr_t)target_move);
}


static void
board_handicap_stone(board_t *board, int x, int y, move_queue_t *q)
{
	move_t m = move(coord_xy(x, y), S_BLACK);

	int r = board_play(board, &m);  assert(r >= 0);

	if (q)  mq_add(q, m.coord, 0);
}

void
board_handicap(board_t *board, int stones, move_queue_t *q)
{
	assert(stones <= 9);
	int margin = 3 + (board_rsize(board) >= 13);
	int min = margin;
	int mid = board_stride(board) / 2;
	int max = board_stride(board) - 1 - margin;
	const int places[][2] = {
		{ min, min }, { max, max }, { max, min }, { min, max }, 
		{ min, mid }, { max, mid },
		{ mid, min }, { mid, max },
		{ mid, mid },
	};

	board->handicap = stones;

	if (stones == 5 || stones == 7) {
		board_handicap_stone(board, mid, mid, q);
		stones--;
	}

	int i;
	for (i = 0; i < stones; i++)
		board_handicap_stone(board, places[i][0], places[i][1], q);
}


/********************************************************************************************************/
/* playout board logic */

bool
board_permit(board_t *b, move_t *m, void *data)
{
	if (unlikely(board_is_one_point_eye(b, m->coord, m->color)) /* bad idea to play into one, usually */
	    || !board_is_valid_move(b, m))
		return false;
	return true;
}

static inline bool
board_try_random_move(board_t *b, enum stone color, coord_t *coord, int f, ppr_permit permit, void *permit_data)
{
	*coord = b->f[f];
	move_t m = { *coord, color };
	if (DEBUGL(6))
		fprintf(stderr, "trying random move %d: %d,%d %s %d\n", f, coord_x(*coord), coord_y(*coord), coord2sstr(*coord), board_is_valid_move(b, &m));
	permit = (permit ? permit : board_permit);
	if (!permit(b, &m, permit_data))
		return false;
	if (m.coord == *coord)
		return likely(board_play_f(b, &m, f) >= 0);
	*coord = m.coord; // permit modified the coordinate
	return likely(board_play(b, &m) >= 0);
}

void
board_play_random(board_t *b, enum stone color, coord_t *coord, ppr_permit permit, void *permit_data)
{
	if (likely(b->flen)) {
		int base = fast_random(b->flen), f;
		for (f = base; f < b->flen; f++)
			if (board_try_random_move(b, color, coord, f, permit, permit_data))
				return;
		for (f = 0; f < base; f++)
			if (board_try_random_move(b, color, coord, f, permit, permit_data))
				return;
	}

	*coord = pass;
	move_t m = { pass, color };
	board_play(b, &m);
}


/********************************************************************************************************/

/* XXX: We attempt false eye detection but we will yield false
 * positives in case of http://senseis.xmp.net/?TwoHeadedDragon :-( */
bool
board_is_false_eyelike(board_t *board, coord_t coord, enum stone eye_color)
{
	int color_diag_libs[S_MAX] = {0, 0, 0, 0};

	foreach_diag_neighbor(board, coord) {
		color_diag_libs[board_at(board, c)]++;
	} foreach_diag_neighbor_end;
	
	/* For false eye, we need two enemy stones diagonally in the
	 * middle of the board, or just one enemy stone at the edge
	 * or in the corner. */
	color_diag_libs[stone_other(eye_color)] += !!color_diag_libs[S_OFFBOARD];
	return color_diag_libs[stone_other(eye_color)] >= 2;
}

bool
board_is_one_point_eye(board_t *b, coord_t c, enum stone eye_color)
{
	return (board_is_eyelike(b, c, eye_color) &&
		!board_is_false_eyelike(b, c, eye_color));
}

enum stone
board_eye_color(board_t *b, coord_t c)
{
	if (board_is_eyelike(b, c, S_WHITE))  return S_WHITE;
	if (board_is_eyelike(b, c, S_BLACK))  return S_BLACK;
	return S_NONE;
}

floating_t
board_fast_score(board_t *board)
{
	int scores[S_MAX] = { 0, };
	
	foreach_point(board) {
		enum stone color = board_at(board, c);
		if (color == S_NONE && board->rules != RULES_STONES_ONLY)
			color = board_eye_color(board, c);
		scores[color]++;
		// fprintf(stderr, "%d, %d ++%d = %d\n", coord_x(c), coord_y(c), color, scores[color]);
	} foreach_point_end;

	return board_score(board, scores);
}

/* One flood-fill iteration.
 * Empty spots start with value -1 initially (unset). 
 * Returns true if next iteration is required. */
static bool
board_tromp_taylor_iter(board_t *board, int *ownermap)
{
	bool needs_update = false;
	foreach_free_point(board) {
		/* Ignore occupied and already-dame positions. */
		assert(board_at(board, c) == S_NONE);
		if (board->rules == RULES_STONES_ONLY)
		    ownermap[c] = FO_DAME;
		if (ownermap[c] == FO_DAME)
			continue;
		
		/* Count neighbors. */
		int nei[4] = {0};
		foreach_neighbor(board, c, {
			nei[ownermap[c]]++;
		});
		
		/* If we have neighbors of both colors, or dame, we are dame too. */
		if ((nei[S_BLACK] && nei[S_WHITE]) || nei[FO_DAME]) {
			ownermap[c] = FO_DAME;
			foreach_neighbor(board, c, {  /* Speed up the propagation. */
				if (board_at(board, c) == S_NONE)
					ownermap[c] = FO_DAME;
			});
			needs_update = true;
			continue;
		}
		
		/* If we have neighbors of one color, we are owned by that color, too. */
		if (ownermap[c] == -1 && (nei[S_BLACK] || nei[S_WHITE])) {
			int newowner = (nei[S_BLACK] ? S_BLACK : S_WHITE);
			ownermap[c] = newowner;
			foreach_neighbor(board, c, {  /* Speed up the propagation. */
				if (board_at(board, c) == S_NONE && ownermap[c] == -1)
					ownermap[c] = newowner;
			});
			needs_update = true;
			continue;
		}
	} foreach_free_point_end;
	return needs_update;
}

static int
board_score_handicap_compensation(board_t *b)
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

/* Score from white perspective, taking rules / handicap into account.
 * scores[]: number of points controlled by black/white. */
floating_t
board_score(board_t *b, int scores[S_MAX])
{
	int handi_comp = board_score_handicap_compensation(b);
	floating_t score = b->komi + handi_comp + scores[S_WHITE] - scores[S_BLACK];

	/* Aja's formula for converting area scoring to territory:
	 *   http://computer-go.org/pipermail/computer-go/2010-April/000209.html
	 * Under normal circumstances there's a relationship between area
	 * and territory scoring so we can derive one from the other. If
	 * the board has been artificially edited however the relationship
	 * is broken and japanese score will be off. */
	if (b->rules == RULES_JAPANESE)
		score += (last_move(b).color == S_BLACK) + (b->passes[S_WHITE] - b->passes[S_BLACK]);
	return score;
}

static void
final_ownermap_printhook(board_t *board, coord_t c, strbuf_t *buf, void *data)
{
	int *ownermap = (int*)data;
	
	if (c == pass)  /* Stuff to display in header */
		return;
	
        const char chr[] = ":XO#";
        sbprintf(buf, "%c ", chr[ownermap[c]]);
}

void
board_print_official_ownermap(board_t *b, move_queue_t *dead)
{
	int dame, seki;
	int ownermap[board_max_coords(b)];
	board_official_score_details(b, dead, &dame, &seki, ownermap, NULL);

        board_print_custom(b, stderr, final_ownermap_printhook, ownermap);
}

/* Official score after removing dead groups and Tromp-Taylor counting.
 * Returns number of dames, sekis, final ownermap in @dame, @seki, @ownermap.
 * (only distinguishes between dames/sekis if @po is not NULL) 
 * final ownermap values:  FO_DAME  S_BLACK  S_WHITE  S_OFFBOARD */
floating_t
board_official_score_details(board_t *b, move_queue_t *dead,
			     int *dame, int *seki, int *ownermap, ownermap_t *po)
{
	/* A point P, not colored C, is said to reach C, if there is a path of
	 * (vertically or horizontally) adjacent points of P's color from P to
	 * a point of color C.
	 *
	 * A player's score is the number of points of her color, plus the
	 * number of empty points that reach only her color. */

	int s[S_MAX] = {0};
	const int tr[4] = {-1, 1, 2, 3};  /* -1: unset */
	foreach_point(b) {
		ownermap[c] = tr[board_at(b, c)];
		s[board_at(b, c)]++;
	} foreach_point_end;

	if (dead) {
		/* Process dead groups. */
		for (unsigned int i = 0; i < dead->moves; i++) {
			foreach_in_group(b, dead->move[i]) {
				enum stone color = board_at(b, c);
				ownermap[c] = stone_other(color);
				s[color]--; s[stone_other(color)]++;
			} foreach_in_group_end;
		}
	}

	/* We need to special-case empty board. */
	if (!s[S_BLACK] && !s[S_WHITE])
		return b->komi;

	while (board_tromp_taylor_iter(b, ownermap))
		/* Flood-fill... */;

	int scores[S_MAX] = {0};
	foreach_point(b) {
		assert(ownermap[c] != -1);
		scores[ownermap[c]]++;
	} foreach_point_end;
	*dame = scores[FO_DAME];
	*seki = 0;

	if (po) {
		foreach_point(b) {
			if (ownermap_judge_point(po, c, GJ_THRES) != PJ_SEKI)  continue;
			(*seki)++;  (*dame)--;
		} foreach_point_end;
	}

	return board_score(b, scores);
}

floating_t
board_official_score(board_t *b, move_queue_t *dead)
{
	int dame, seki;
	int ownermap[board_max_coords(b)];
	return board_official_score_details(b, dead, &dame, &seki, ownermap, NULL);
}

/* Returns static buffer */
char *
board_official_score_str(board_t *b, move_queue_t *dead)
{
	static char buf[32];
	floating_t score = board_official_score(b, dead);
	
	if      (score == 0)  sprintf(buf, "0");
	else if (score > 0)   sprintf(buf, "W+%.1f", score);
	else                  sprintf(buf, "B+%.1f", -score);
	return buf;
}

floating_t
board_official_score_color(board_t *b, move_queue_t *dead, enum stone color)
{
	floating_t score = board_official_score(b, dead);
	return (color == S_WHITE ? score : -score);
}

bool
board_set_rules(board_t *board, const char *name)
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
rules2str(enum rules rules)
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


/********************************************************************************************************/
/* board_play() implementation */

static inline void
board_addf(board_t *b, coord_t c)
{
	b->fmap[c] = b->flen; 
	b->f[b->flen++] = c;
}

static inline void
board_rmf(board_t *b, int f)
{
	/* Not bothering to delete fmap records,
	 * Just keep the valid ones up to date. */
	coord_t c = b->f[f] = b->f[--b->flen];
	b->fmap[c] = f;
}

static void
board_commit_move(board_t *b, move_t *m)
{
	if (!playout_board(b)) {
#ifdef DCNN_DARKFOREST
		if (darkforest_dcnn && !is_pass(m->coord))
			b->moveno[m->coord] = b->moves;
#endif
	}

	b->last_move_i = last_move_nexti(b);
	last_move(b) = *m;

	b->moves++;
}

/* Update board hash with given coordinate. */
static void profiling_noinline
board_hash_update(board_t *board, coord_t coord, enum stone color)
{
	if (!playout_board(board)) {
		board->hash ^= hash_at(coord, color);
		if (DEBUGL(8))
			fprintf(stderr, "board_hash_update(%d,%d,%d) ^ %" PRIhash " -> %" PRIhash "\n", color, coord_x(coord), coord_y(coord), hash_at(coord, color), board->hash);
	}

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
board_hash_commit(board_t *b)
{
	if (playout_board(b))  return;

	if (DEBUGL(8)) fprintf(stderr, "board_hash_commit %" PRIhash "\n", b->hash);

	for (int i = 0; i < BOARD_HASH_HISTORY; i++) {
		if (b->hash_history[i] == b->hash) {
			if (DEBUGL(5))  fprintf(stderr, "SUPERKO VIOLATION noted at %s\n", coord2sstr(last_move(b).coord));
			b->superko_violation = true;
			return;
		}
	}

	int i = b->hash_history_next;
	b->hash_history[i] = b->hash;
	b->hash_history_next = (i+1) % BOARD_HASH_HISTORY;
}

static inline void
board_pat3_reset(board_t *b, coord_t c)
{
#ifdef BOARD_PAT3
	b->pat3[c] = pattern3_hash(b, c);
#endif
}

static inline void
board_pat3_fix(board_t *b, group_t group_from, group_t group_to)
{
#ifdef BOARD_PAT3
	group_info_t *gi_from = &board_group_info(b, group_from);
	group_info_t *gi_to = &board_group_info(b, group_to);
	
	if (gi_to->libs == 1) {
		coord_t lib = board_group_info(b, group_to).lib[0];
		if (gi_from->libs == 1) {
			/* We removed group_from from capturable groups,
			 * therefore switching the atari flag off.
			 * We need to set it again since group_to is also
			 * capturable. */
			int fn__i = 0;
			foreach_neighbor(b, lib, {
				b->pat3[lib] |= (group_at(b, c) == group_from) << (16 + 3 - fn__i);
				fn__i++;
			});
		}
	}
#endif /* BOARD_PAT3 */
}

static void
board_capturable_add(board_t *board, group_t group, coord_t lib)
{
	//fprintf(stderr, "group %s cap %s\n", coord2sstr(group), coord2sstr(lib));

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
	assert(board->clen < BOARD_MAX_GROUPS);
	board->c[board->clen++] = group;
#endif
}

static void
board_capturable_rm(board_t *board, group_t group, coord_t lib)
{
	//fprintf(stderr, "group %s nocap %s\n", coord2sstr(group), coord2sstr(lib));
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
	fprintf(stderr, "rm of bad group %s\n", coord2sstr(group_base(group)));
	assert(0);
#endif
}


#define FULL_BOARD
#include "board_play.h"

int
board_play(board_t *b, move_t *m)
{
#ifdef BOARD_UNDO_CHECKS
        assert(!b->quicked);
#endif

	return board_play_(b, m);
}
