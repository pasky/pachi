#define DEBUG
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "dcnn.h"
#include "pattern/pattern.h"
#include "tactics/util.h"
#include "tactics/2lib.h"
#include "tactics/selfatari.h"
#include "board_undo.h"
#include "josekifix/josekifix.h"


static bool dcnn_blunder_enabled = true;
void disable_dcnn_blunder(void)  {  dcnn_blunder_enabled = false;  }

void dcnn_blunder_init(void)
{
	if (DEBUGL(1) && !dcnn_blunder_enabled)
		fprintf(stderr, "Disabled dcnn blunder filtering\n");
}


/**********************************************************************************/

static float
dcnn_max_value(board_t *b, float result[])
{
	float max = 0.0;
	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		max = MAX(max, result[k]);
	} foreach_free_point_end;

	return max;
}

/* Rescale dcnn values so that probabilities add up to 1.0 again */
static void
dcnn_rescale_values(board_t *b, float result[])
{
	float total = 0;
	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		total += result[k];
	} foreach_free_point_end;

	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		result[k] /= total;
	} foreach_free_point_end;
}


/**********************************************************************************/

/* Prevent silly first-line connect blunders where group can be captured afterwards.
 * See t-unit/first_line_blunder.t for examples */
bool
dcnn_first_line_connect_blunder(board_t *b, move_t *m)
{
	/* First-line connect blunder ? */
	if (coord_edge_distance(m->coord) != 0)  return false;
	with_move(b, m->coord, m->color, {
		group_t g = group_at(b, m->coord);
		if (!g)  break;
		if (group_stone_count(b, g, 4) < 3)  break;
		
		/*   # . * .
		 *   # . O X     really stupid first-line connect blunder:
		 *   # O)O X     can capture right away
		 *   # O X X
		 *   # X . .
		 *   # . . .     */
		if (board_group_info(b, g).libs == 2 && can_capture_2lib_group(b, g, NULL, 0))
			with_move_return(true);
		
		/* 3 libs case */
		if (board_group_info(b, g).libs != 3)  break;
		for (int i = 0; i < board_group_info(b, g).libs; i++) {
			coord_t c = board_group_info(b, g).lib[i];
			move_t m2 = move(c, stone_other(m->color));
			if (pattern_match_l1_blunder_punish(b, &m2) != -1)
				with_move_return(true);
		}
	});

	return false;
}

/*  7 | . . . . . . . . 
    6 | . . . X . . . .    Prevent w B3 and C1 blunders, happens sometimes in handicap games
    5 | . . . . . . . .
    4 | . . X X . O . .    w wants to play B2 later here (endgame)
    3 | . . O X X O . .
    2 | . . X O O . . . 
    1 | . . . . . . . .    Ex:  t-unit/dcnn_blunder.t
      +-----------------        t-regress/4-4_reduce_3-3
        A B C D E F G H    */
static bool
dcnn_44_reduce_33_blunder(board_t *b, move_t *m, move_t *redirect)
{
	/* B3 not blunder if w has a stone at B6, make sure override covers that area. */
	override_t override = { .coord_empty = "B1", .prev = "pass", "B2", "4-4 reduce 3-3", { 0x104718b6711a28d0, 0xdcd0e566177a90e8, 0xb1256f54939c1c48, 0xce86cd889eb98e38,
											       0xd39f04865100718, 0x8dfd49f239c658, 0xa84411bdaafa8a10, 0xbd0cd1b2a8ace9b8 } };
	int dist = coord_edge_distance(m->coord);
	if (dist != 1 && dist != 0)  return false;

	/* Check if m is like w B3 or C1 */
	coord_t b3 = str2coord("B3");
	coord_t c1 = str2coord("C1");
	for (int rot = 0; rot < 8; rot++) {
		coord_t rb3 = rotate_coord(b3, rot);
		coord_t rc1 = rotate_coord(c1, rot);
		
		/* Check we're in the right quadrant for m */
		if (m->coord != rb3 && m->coord != rc1)  continue;
		
		coord_t c = check_override_rot(b, &override, rot, 0);
		if (!is_pass(c)) {		/* Would rather just clobber since w doesn't want to play B2 right away, */
			redirect->coord = c;	/* but mcts ends up playing B3 anyway sometimes in this case ! */
			return true;		/* So redirect, if it had a big prior will play B2 right away, */
		}				/* no big deal. */
	}

	return false;
}

/*    . . X X X X O O O		Prevent blunders making big 2 libs groups that can be captured.
 *    . X . . . O O X O
 *    X X O O O O X X O		XXX false positives
 *    X . X X X O)X O .             - will also block nakade moves that are useful to kill a group.
 *    . X . . . * X O .               fortunately mcts is good at finding them, looks ok if missing from dcnn output.
 *    . . . . . . X O .             - other cases ?
 *    . X . . X X O O .		
 *    . . . X O O . O .		
 *    . . . X O . O . .		Ex:  t-unit/dcnn_blunder.t  t-regress/atari_ladder_big4  */
static bool
dcnn_group_2lib_blunder(board_t *b, move_t *m)
{
	enum stone color = m->color;
	
	with_move(b, m->coord, color, {
		group_t g = group_at(b, m->coord);
		if (!g)  break;
		if (board_group_info(b, g).libs != 2)  break;	/* 2 libs */
		if (group_stone_count(b, g, 4) < 3)    break;	/* creates own group with at least 3 stones */
		if (can_capture_2lib_group(b, g, NULL, 0))	/* can be captured now */
			with_move_return(true);
	});
	return false;
}


/**********************************************************************************/
/* Atari defense move boosting */

#define MAX_ATARIS 15

typedef struct {
	coord_t		coord;
	coord_t         fixed_ataris[MAX_ATARIS];
	int             nfixed_ataris;
	float           dist;		/* distance to closest fixed atari */
} defense_move_t;

static int
candidate_dist_cmp(const void *pa, const void *pb)
{
	const defense_move_t *a = pa;
	const defense_move_t *b = pb;
	if (a->dist == b->dist)
		return 0;
	return (a->dist < b->dist ? -1 : 1);
}

static int
keep_closest_moves(defense_move_t candidates[], int ncandidates, float result[], move_queue_t *defense_moves)
{
	/* Sort defense_moves by distance to corresponding atari move */
	qsort(candidates, ncandidates, sizeof(*candidates), candidate_dist_cmp);

	/* Keep first 5 and dcnn moves */
	defense_moves->moves = 0;
	for (int i = 0; i < ncandidates; i++) {
		coord_t c = candidates[i].coord;
		int k = coord2dcnn_idx(c);
		//fprintf(stderr, "%s dist %.1f\n", coord2sstr(c), coord_dist[i].dist);
		if (i >= 5 && result[k] < 0.02)		// throw away
			continue;

		mq_add(defense_moves, c, 0);
	}
	
	return defense_moves->moves;
}

/* Keep best combos only. Most of the time this does nothing as there will be a
 * single atari to defend, but in case where there are multiple ataris this can
 * get rid of some silly moves. For example:
 *
 *    N O P Q R S T
 *    . . . . . . . | 10      S5 defends ataris S7
 *    . . . O O O . |  9      T5 defends ataris S7
 *    . . X O X X . |  8      S6 defends ataris S6 S7   ->  keep S6 S7 T7 T8  (best combos)
 *    . . X X O . . |  7      T6 defends ataris S7          drop S5 T5 T6     (only fix S7)
 *    . . X O O . . |  6      S7 defends ataris S6 S7
 *    . . . X X . . |  5      T7 defends ataris S6 S7
 *    . . . . . O . |  4      T8 defends ataris S6 S7
 *    . . . . . . . |  3					*/
static int
keep_best_combos(defense_move_t candidates[], int ncandidates, int best_combos[])
{
	for (int i = 0; i < ncandidates; i++) {
		defense_move_t *def = &candidates[i];
		coord_t atari = def->fixed_ataris[0];
		if (def->nfixed_ataris == 1 && best_combos[atari] > 1)
			candidates[i--] = candidates[--ncandidates];	  /* drop it */
	}

	return ncandidates;
}

/* Find all @feature atari moves on the board. */
static int
find_atari_moves(move_queue_t *q, int feature, board_t *b, enum stone to_play, ownermap_t *ownermap)
{
	int n = 0;
	
	foreach_free_point(b) {
		move_t m = move(c, to_play);
		if (pattern_match_atari(b, &m, ownermap) == feature) {
			n++;
			if (q)  mq_add(q, c, 0);
		}
	} foreach_free_point_end;

	return n;
}

static void
show_which_move_fixes_which_atari(coord_t c, move_queue_t *fixed)
{
	fprintf(stderr, "dcnn_blunder: %s fixes ataris ", coord2sstr(c));
	for (int i = 0; i < fixed->moves; i++)
		fprintf(stderr, "%s ", coord2sstr(fixed->move[i]));
	fprintf(stderr, "\n");
}

static bool
really_defends_atari(board_t *b, board_t *orig_board, enum stone color, coord_t atari)
{
	move_queue_t targets;
	board_get_2lib_neighbors(orig_board, atari, color, &targets);
	
	/* If multiple target groups should at least defend one,
	 * but don't let a big group get captured */
	bool found = false;
	for (int i = 0; i < targets.moves; i++) {
		group_t g = group_at(b, targets.move[i]);  assert(g);
		int libs = board_group_info(b, g).libs;  /* May not have 2 libs anymore */
		bool can_cap = (libs == 2 && can_capture_2lib_group(b, g, NULL, 0));
		if (can_cap && group_stone_count(b, g, 4) >= 3)
			return false;
		if (!can_cap)
			found = true;
	}
	return found;
}

static int
find_atari_defense_moves(int feature,
			 board_t *b, enum stone color, ownermap_t *ownermap,
			 defense_move_t candidates[], int best_combos[])
{
	enum stone other_color = stone_other(color);
	move_queue_t atari_moves;  mq_init(&atari_moves);
	int n = find_atari_moves(&atari_moves, feature, b, other_color, ownermap);
	if (!n)  return 0;

	board_t orig_board;  board_copy(&orig_board, b);
	
	int ncandidates = 0;
	foreach_free_point(b) {
		if (is_selfatari(b, color, c))
			continue;
		
		with_move(b, c, color, {
			move_queue_t atari_moves2;  mq_init(&atari_moves2);
			if (find_atari_moves(&atari_moves2, feature, b, other_color, ownermap) >= n)
				break;		/* doesn't help */

			/* Found potential atari defense move. Look closer so we can filter later on. */
			
			/* Find which atari(s) got changed/fixed */
			move_queue_t fixed;  mq_init(&fixed);
			mq_sub(&atari_moves, &atari_moves2, &fixed);

			/* Check move really defends something. Move may be really stupid
			 * actually, like changing a ladder to a snapback or an atari_and_cap
			 * to a double atari */
			for (int i = 0; i < fixed.moves; i++)
				if (!really_defends_atari(b, &orig_board, color, fixed.move[i]))
					mq_remove_index(&fixed, i--);	/* Delete */
			if (!fixed.moves)  break;
			
			if (DEBUGL(4))  show_which_move_fixes_which_atari(c, &fixed);
			
			/* Store defense move, corresponding ataris, distance, and keep track of best combos */
			defense_move_t *def = &candidates[ncandidates++];
			def->coord = c;
			def->dist = 9999999;
			for (int i = 0; i < fixed.moves && i < MAX_ATARIS; i++) {
				coord_t atari = fixed.move[i];
				def->fixed_ataris[i] = atari;
				def->nfixed_ataris++;
				def->dist = MIN(def->dist, coord_distance(atari, c));
				best_combos[atari] = MAX((int)fixed.moves, best_combos[atari]);
			}
		});
	} foreach_free_point_end;

	return ncandidates;
}

static int
select_atari_defense_moves(char *name, int feature,
			   board_t *b, enum stone color, float result[], ownermap_t *ownermap,
			   move_queue_t *defense_moves, int *dropped)
{
	defense_move_t candidates[BOARD_MAX_MOVES];        memset(candidates, 0, sizeof(candidates));
	int            best_combos[BOARD_MAX_COORDS];      memset(best_combos, 0, sizeof(best_combos));
	int n = find_atari_defense_moves(feature, b, color, ownermap, candidates, best_combos);
	if (!n)  return 0;

	/* Found atari defense moves. Could stop here and just boost all of them,
	 * but can be tens of them (ladder breakers) and boosting becomes less
	 * effective the more moves we boost, so keep only a few. */
	
	/* Keep best combos only */
	int moves = keep_best_combos(candidates, n, best_combos);
	
	/* Just keep close ones and dcnn moves */
	moves = keep_closest_moves(candidates, moves, result, defense_moves);

	*dropped = (n - moves);
	return moves;
}

static void
boost_atari_defense_short_log(char *name, board_t *b, float result[], move_queue_t *defense_moves, int dropped)
{
	int moves = defense_moves->moves;
	int best_n = 12;
	coord_t best_c[12];
	float   best_r[12];
	get_dcnn_best_moves(b, result, best_c, best_r, best_n);
	
	fprintf(stderr, "dcnn blunder: boosted [ ");
	for (int i = 0; i < best_n; i++) {
		if (!mq_has(defense_moves, best_c[i]))  continue;
		const char *str = (is_pass(best_c[i]) ? "" : coord2sstr(best_c[i]));
		fprintf(stderr, "%s ", str);
	}
	if (moves > best_n)  fprintf(stderr, "... ] (%s) (%i moves)", name, moves);
	else		     fprintf(stderr, "] (%s)", name);
	if (dropped)         fprintf(stderr, " (dropped %i)", dropped);
	fprintf(stderr, "\n");
}

static int
boost_atari_defense_moves(char *name, int feature,
			  board_t *b, enum stone color, float result[], ownermap_t *ownermap, int debugl)
{
	move_queue_t defense_moves;	mq_init(&defense_moves);
	int          dropped;
	int          moves = select_atari_defense_moves(name, feature, b, color, result, ownermap,
							&defense_moves, &dropped);
	if (!moves)  return 0;
	
	/* Now we know how many moves will get boosted */
	float maxres = dcnn_max_value(b, result);
	bool  log_verbose = (debugl && DEBUGL(3));	/* log each move individually */
	for (int i = 0; i < defense_moves.moves; i++) {
		coord_t c = defense_moves.move[i];
		int k = coord2dcnn_idx(c);
		float newres = maxres + 0.2 * moves + result[k];
		if (log_verbose)  fprintf(stderr, "dcnn blunder: boost %-3s  %i%% -> %i%%  (%s)\n",
					  coord2sstr(c), (int)(result[k] * 100), (int)(newres * 100), name);
		result[k] = newres;
	}
	
	dcnn_rescale_values(b, result);
	
	if (!log_verbose)	/* short log (one line) */
		boost_atari_defense_short_log(name, b, result, &defense_moves, dropped);

	return moves;
}


/**********************************************************************************/

/* Check if move m is a dcnn blunder.
 * Return true:                 clobber move
 * Return true + set redirect:  redirect dcnn prior and clobber move */
static bool
dcnn_blunder(board_t *b, move_t *m, float r, move_t *redirect, char **name)
{
	if (r < 0.01)  return false;
	if (board_playing_ko_threat(b))  return false;

	*name = "first line connect blunder";
	if (dcnn_first_line_connect_blunder(b, m))      return true;
	
	*name = "4-4 reduce 3-3 blunder";
	if (dcnn_44_reduce_33_blunder(b, m, redirect))  return true;

	*name = "group 2lib blunder";
	if (dcnn_group_2lib_blunder(b, m))	        return true;
	
	return false;
}

/* Fix dcnn blunders by altering dcnn priors before they get used.
 * (last resort, for moves which are a bad fit for a joseki override) */
int
dcnn_fix_blunders(board_t *b, enum stone color, float result[], ownermap_t *ownermap, bool debugl)
{
	if (!dcnn_blunder_enabled)
		return 0;
	
	/* Make ownermap if caller didn't provide one */
	if (!ownermap) {
		ownermap = alloca(sizeof(*ownermap));
		ownermap_init(ownermap);
		mcowner_playouts(b, color, ownermap);
	}

	float blunder_rating = 0.001;  /* 0.1% */
	int changes = 0;

	changes += boost_atari_defense_moves("atari and cap defense", PF_ATARI_AND_CAP, b, color, result, ownermap, debugl);
	changes += boost_atari_defense_moves("atari and cap2 defense", PF_ATARI_AND_CAP2, b, color, result, ownermap, debugl);
	changes += boost_atari_defense_moves("atari ladder big defense", PF_ATARI_LADDER_BIG, b, color, result, ownermap, debugl);
	
	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		move_t m = move(c, color);
		move_t redirect = move(pass, color);
		char *name = "";
		
		if (!dcnn_blunder(b, &m, result[k], &redirect, &name))
			continue;
		
		if (redirect.coord != pass) {		/* redirect + clobber */
			int k2 = coord2dcnn_idx(redirect.coord);
			result[k2] += result[k];
			if (debugl)  fprintf(stderr, "dcnn blunder: replaced %-3s -> %-3s  (%i%%)  (%s)\n",
					     coord2sstr(c), coord2sstr(redirect.coord), (int)(result[k] * 100), name);
		}
		else					/* clobber */
			if (debugl)  fprintf(stderr, "dcnn blunder: fixed %-3s  %i%% -> %i%%  (%s)\n",
					     coord2sstr(c), (int)(result[k] * 100), (int)(blunder_rating * 100), name);
		
		result[k] = blunder_rating;
		changes++;
	} foreach_free_point_end;

	if (changes)
		dcnn_rescale_values(b, result);
		
	if (changes && debugl) {
		coord_t best_c[DCNN_BEST_N];
		float   best_r[DCNN_BEST_N];
		get_dcnn_best_moves(b, result, best_c, best_r, DCNN_BEST_N);
		print_dcnn_best_moves(b, best_c, best_r, DCNN_BEST_N);
	}
	
	return changes;
}

/* For testing: Return list of boosted or killed blunders in list @q.
 * @boosted selects what to return. */
void
get_dcnn_blunders(bool boosted, board_t *b, enum stone color, float result[], ownermap_t *ownermap, move_queue_t *q)
{
	if (boosted) {
		int dropped;		/* keep in sync with dcnn_fix_blunders() calls */
		select_atari_defense_moves("atari and cap defense", PF_ATARI_AND_CAP, b, color, result, ownermap, q, &dropped);
		select_atari_defense_moves("atari ladder big defense", PF_ATARI_LADDER_BIG, b, color, result, ownermap, q, &dropped);
	}

	/* boosted = false :  add killed moves
	 * boosted = true  :  remove killed moves from boosted list */
	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		move_t m = move(c, color);
		move_t redirect = move(pass, color);
		char *name = "";
		
		if (dcnn_blunder(b, &m, result[k], &redirect, &name)) {
			if (!boosted)  mq_add(q, c, 0);
			else           mq_remove(q, c);
		}
	} foreach_free_point_end;
}
