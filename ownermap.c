#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "mq.h"
#include "tactics/1lib.h"
#include "ownermap.h"

void
ownermap_init(ownermap_t *ownermap)
{
	memset(ownermap, 0, sizeof(*ownermap));
}

static void
printhook(board_t *board, coord_t c, strbuf_t *buf, void *data)
{
        ownermap_t *ownermap = (ownermap_t*)data;

	if (c == pass) { /* Stuff to display in header */
		if (!ownermap || !ownermap->playouts) return;
		sbprintf(buf, "Score Est: %s", ownermap_score_est_str(board, ownermap));
		return;
	}
	
        if (!ownermap) {  sbprintf(buf, ". ");  return;  }
	
        const char chr[] = ":XO,"; // seki, black, white, unclear
        const char chm[] = ":xo,";
        char ch = chr[ownermap_judge_point(ownermap, c, GJ_THRES)];
        if (ch == ',')   // less precise estimate then?
                ch = chm[ownermap_judge_point(ownermap, c, 0.67)];	
        sbprintf(buf, "%c ", ch);
}

void
board_print_ownermap(board_t *b, FILE *f, ownermap_t *ownermap)
{
        board_print_custom(b, f, printhook, ownermap);
}

void
ownermap_fill(ownermap_t *ownermap, board_t *b)
{
	ownermap->playouts++;
	foreach_point(b) {
		enum stone color = board_at(b, c);
		if (color == S_OFFBOARD)  continue;
		if (color == S_NONE)      color = board_eye_color(b, c);
		ownermap->map[c][color]++;
	} foreach_point_end;
}

float
ownermap_estimate_point(ownermap_t *ownermap, coord_t c)
{
	assert(ownermap->map);
	assert(!is_pass(c));
	int b = ownermap->map[c][S_BLACK];
	int w = ownermap->map[c][S_WHITE];
	int total = ownermap->playouts;
	return 1.0 * (b - w) / total;
}

enum point_judgement
ownermap_judge_point(ownermap_t *ownermap, coord_t c, floating_t thres)
{
	assert(ownermap->map);
	assert(!is_pass(c));
	int n = ownermap->map[c][S_NONE];
	int b = ownermap->map[c][S_BLACK];
	int w = ownermap->map[c][S_WHITE];
	int total = ownermap->playouts;
	if      (n     >= total * thres)  return PJ_SEKI;
	else if (n + b >= total * thres)  return PJ_BLACK;
	else if (n + w >= total * thres)  return PJ_WHITE;
	else                              return PJ_UNKNOWN;
}

enum stone
ownermap_color(ownermap_t *ownermap, coord_t c, floating_t thres)
{
	enum stone colors[4] = {S_NONE, S_BLACK, S_WHITE, S_NONE };
	enum point_judgement pj = ownermap_judge_point(ownermap, c, thres);
	return colors[pj];
}

void
ownermap_judge_groups(board_t *b, ownermap_t *ownermap, group_judgement_t *judge)
{
	assert(ownermap->map);
	assert(judge->gs);
	memset(judge->gs, GS_NONE, board_max_coords(b) * sizeof(judge->gs[0]));

	foreach_point(b) {
		enum stone color = board_at(b, c);
		group_t g = group_at(b, c);
		if (!g)  continue;
		enum point_judgement pj = ownermap_judge_point(ownermap, c, judge->thres);

		if (pj == PJ_UNKNOWN) {
			judge->gs[g] = GS_UNKNOWN;
			continue;
		}
		
		if (judge->gs[g] == GS_UNKNOWN)
			continue;
		
		/* Update group state.
		 * Comparing enum types, casting (int) avoids compiler warnings */
		enum gj_state newst;
		if      ((int)pj == (int)color)               newst = GS_ALIVE;
		else if ((int)pj == (int)stone_other(color))  newst = GS_DEAD;
		else                { assert(pj == PJ_SEKI);  newst = GS_UNKNOWN;  /* Exotic! */  }
		
		if      (judge->gs[g] == GS_NONE)  judge->gs[g] = newst;
		else if (judge->gs[g] != newst)    judge->gs[g] = GS_UNKNOWN;  /* Contradiction. :( */
	} foreach_point_end;
}

void
groups_of_status(board_t *b, group_judgement_t *judge, enum gj_state s, move_queue_t *mq)
{
	foreach_point(b) { /* foreach_group, effectively */
		group_t g = group_at(b, c);
		if (!g || g != c) continue;

		assert(judge->gs[g] != GS_NONE);
		if (judge->gs[g] == s)
			mq_add(mq, g, 0);
	} foreach_point_end;
}

void
ownermap_dead_groups(board_t *b, ownermap_t *ownermap, move_queue_t *dead, move_queue_t *unclear)
{
	enum gj_state gs_array[board_max_coords(b)];
	group_judgement_t gj = { 0.67, gs_array };
	ownermap_judge_groups(b, ownermap, &gj);
	if (dead)     {  dead->moves = 0;     groups_of_status(b, &gj, GS_DEAD, dead);  }
	if (unclear)  {  unclear->moves = 0;  groups_of_status(b, &gj, GS_UNKNOWN, unclear);  }
}

void
ownermap_scores(board_t *b, ownermap_t *ownermap, int *scores)
{
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		enum point_judgement j = ownermap_judge_point(ownermap, c, 0.67);
		scores[j]++;
	} foreach_point_end;
}

int
ownermap_dames(board_t *b, ownermap_t *ownermap)
{
	int scores[S_MAX] = { 0, };
	ownermap_scores(b, ownermap, scores);
	return scores[PJ_UNKNOWN];
}

enum point_judgement
ownermap_score_est_coord(board_t *b, ownermap_t *ownermap, coord_t c)
{
	enum point_judgement j = ownermap_judge_point(ownermap, c, 0.67);
	enum stone s = board_at(b, c);
	
	/* If status is unclear and there's a stone there assume it's alive. */
	if (j != PJ_BLACK && j != PJ_WHITE && (s == S_BLACK || s == S_WHITE))
		return (enum point_judgement)s;
	return j;
}

float
ownermap_score_est(board_t *b, ownermap_t *ownermap)
{
	int scores[S_MAX] = {0, };  /* Number of points owned by each color */
	
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)  continue;
		enum point_judgement j = ownermap_score_est_coord(b, ownermap, c);
		scores[j]++;
	} foreach_point_end;

	return board_score(b, scores);
}

float
ownermap_score_est_color(board_t *b, ownermap_t *ownermap, enum stone color)
{
	floating_t score = ownermap_score_est(b, ownermap);
	return (color == S_BLACK ? -score : score);
}

/* Returns static buffer */
char *
ownermap_score_est_str(board_t *b, ownermap_t *ownermap)
{
	static char buf[32];
	float s = ownermap_score_est(b, ownermap);
	sprintf(buf, "%s+%.1f", (s > 0 ? "W" : "B"), fabs(s));
	return buf;
}

static bool
border_stone(board_t *b, coord_t c, int *final_ownermap)
{
	enum stone color = board_at(b, c);
	foreach_neighbor(b, c, {
		if (board_at(b, c) == stone_other(color) &&
		    final_ownermap[c] == (int)stone_other(color))
			return true;
	});
	return false;
}

bool
board_position_final(board_t *b, ownermap_t *ownermap, char **msg)
{
	*msg = "too early to pass";
	if (b->moves < board_earliest_pass(b))
		return false;
	
	move_queue_t dead, unclear;
	ownermap_dead_groups(b, ownermap, &dead, &unclear);
	
	floating_t score_est = ownermap_score_est(b, ownermap);

	int dame, seki;
	int final_ownermap[board_max_coords(b)];
	floating_t final_score = board_official_score_details(b, &dead, &dame, &seki, final_ownermap, ownermap);

	return board_position_final_full(b, ownermap, &dead, &unclear, score_est,
					 final_ownermap, dame, final_score, msg);
}

bool
board_position_final_full(board_t *b, ownermap_t *ownermap,
			  move_queue_t *dead, move_queue_t *unclear, float score_est,
			  int *final_ownermap, int final_dames, float final_score, char **msg)
{
	*msg = "too early to pass";
	if (b->moves < board_earliest_pass(b))
		return false;
	
	/* Unclear groups ? */
	*msg = "unclear groups";
	if (unclear->moves)  return false;

	/* Border stones in atari ? */
	foreach_point(b) {
		group_t g = group_at(b, c);
		if (!g || board_group_info(b, g).libs > 1)  continue;
		if (!border_stone(b, c, final_ownermap))  continue;
		if (capturing_group_is_snapback(b, g))  continue;

		enum stone color = board_at(b, c);
		foreach_neighbor(b, board_group_info(b, g).lib[0], {
			if (final_ownermap[c] != (int)color) continue;
			*msg = "border stones in atari";
			return false;
		});
	} foreach_point_end;

	/* Can't have b&w dead groups next to each other */
	if (dead->moves < 2)  goto skip;
	foreach_point(b) {
		group_t g = group_at(b, c);
		if (!g || !mq_has(dead, g))  continue;
		enum stone other_color = stone_other(board_at(b, c));

		foreach_neighbor(b, c, {
			group_t g2 = group_at(b, c);
			if (!g2 || g2 == g || board_at(b, c) != other_color || !mq_has(dead, g2))
				continue;
			*msg = "b&w dead groups next to each other";
			return false;
		});
	} foreach_point_end;
	
 skip:
	/* Non-seki dames surrounded by only dames / border / one color are no dame to me,
	 * most likely some territories are still open ... */
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD) continue;
		if (final_ownermap[c] != FO_DAME)  continue;
		if (ownermap_judge_point(ownermap, c, GJ_THRES) == PJ_SEKI) continue;

		coord_t dame = c;
		int ne[4] = { 0, };
		foreach_neighbor(b, dame, {
			ne[final_ownermap[c]]++;
		});		
		if (ne[S_BLACK] + ne[FO_DAME] + ne[S_OFFBOARD] == 4 ||
		    ne[S_WHITE] + ne[FO_DAME] + ne[S_OFFBOARD] == 4) {
			static char buf[100];
			sprintf(buf, "non-final position at %s", coord2sstr(dame));
			*msg = buf;
			return false;
		}
	} foreach_point_end;

	/* If ownermap and official score disagree position is likely not final.
	 * If too many dames also. */
	int max_dames = (board_large(b) ? 15 : 7);
	*msg = "non-final position: too many dames";
	if (final_dames > max_dames)    return false;
	
	/* Can disagree up to dame points, as long as there are not too many.
	 * For example a 1 point difference with 1 dame is quite usual... */
	int max_diff = MIN(final_dames, 4);
	*msg = "non-final position: score est and official score don't agree";
	if (fabs(final_score - score_est) > max_diff)  return false;
	
	return true;
}
