#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG

#include "debug.h"
#include "board.h"
#include "move.h"
#include "playout.h"
#include "random.h"
#include "tactics/util.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/tree.h"
#include "uct/dynkomi.h"
#include "uct/uct.h"
#include "uct/walk.h"
#include "uct/prior.h"
#include "gogui.h"

#define DESCENT_DLEN 512


static void
uct_progress_text(FILE *fh, uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts)
{
	int parity = (genmove_pondering(u) ? -1 : 1);
	
	if (!UDEBUGL(0))
		return;

	/* Best move */
	tree_node_t *best = u->policy->choose(u->policy, t->root, b, color, resign);
	if (!best) {
		fprintf(fh, "... No moves left\n");
		return;
	}
	fprintf(fh, "[%d] ", playouts);
	fprintf(fh, "best %.1f%% ", 100 * tree_node_get_value(t, parity, best->u.value));

	/* Dynamic komi */
	if (t->use_extra_komi)
		fprintf(fh, "xkomi %.1f ", t->extra_komi);

	/* Best sequence */
	fprintf(fh, "| seq ");
	for (int depth = 0; depth < 4; depth++) {
		if (best && best->u.playouts >= 25) {
			fprintf(fh, "%3s ", coord2sstr(node_coord(best)));
			best = u->policy->choose(u->policy, best, b, color, resign);
		}
		else    fprintf(fh, "    ");
	}

	/* Best candidates */
	int nbest = 4;
	float   best_r[nbest];
	coord_t best_c[nbest];
	uct_get_best_moves(u, best_c, best_r, nbest, true, 100);

	fprintf(fh, "| can %c ", color == S_BLACK ? 'b' : 'w');
	for (int i = 0; i < nbest; i++) {
		/* fix parity */
		best_r[i] = (parity == 1 ? best_r[i] : 1.0 - best_r[i]);
		if (!is_pass(best_c[i]))
			fprintf(fh, "%3s(%.1f) ", coord2sstr(best_c[i]), 100 * best_r[i]);
		else
			fprintf(fh, "          ");
	}

	/* Tree memory usage */
	if (UDEBUGL(3))
		fprintf(fh, " | %.1fMb", (float)t->nodes_size / 1024 / 1024);
	
	fprintf(fh, "\n");
}

/* Leela-zero format:
 * info move Q16 visits 1 winrate 4687 prior 2198 order 0 pv Q16 [...] */
static void
uct_progress_lz(FILE *fh, uct_t *u, tree_t *t, board_t *b, enum stone color)
{
	tree_node_t *node = u->t->root;

	/* Best candidates */
	int nbest = 20;
	float   best_pl[nbest];	
	float   best_wr[nbest];
	coord_t best_c[nbest];
	uct_get_best_moves(u, best_c, best_pl, nbest, false, 500);
	uct_get_best_moves(u, best_c, best_wr, nbest, true,  500);
	if (is_pass(best_c[0]))  return;

	/* Priors */
	float   best_pr[19 * 19];
	coord_t best_cpr[19 * 19];
	float   priors[BOARD_MAX_COORDS];  memset(priors, 0, sizeof(priors));
	get_node_prior_best_moves(node, best_cpr, best_pr, 19 * 19);
	for (int i = 0; i < 19 * 19; i++)
		if (best_cpr[i] > 0)
			priors[best_cpr[i]] = best_pr[i];

	for (int i = 0; i < nbest && !is_pass(best_c[i]); i++) {
		fprintf(fh, "info move %s visits %i winrate %i prior %i order %i ",
			coord2sstr(best_c[i]), (int)best_pl[i], (int)(best_wr[i] * 10000),
			(int)(priors[best_c[i]] * 10000), i);

		/* Dump best variation */
		fprintf(fh, "pv %s ", coord2sstr(best_c[i]));
		tree_node_t *n = tree_get_node(node, best_c[i]);
		while (1) {
			n = u->policy->choose(u->policy, n, b, color, resign);
			if (!n || n->u.playouts < 100) break;
			fprintf(fh, "%s ", coord2sstr(node_coord(n)));
		}
	}
	fprintf(fh, "\n");
}

/* GoGui live gfx: show best sequence */
static void
uct_progress_gogui_sequence(uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts)
{
	int n = 20;
	coord_t seq[n];   for (int i = 0; i < n; i++)  seq[i] = pass;

	/* Best move */
	tree_node_t *best = u->policy->choose(u->policy, t->root, b, color, resign);
	if (!best) {  fprintf(stderr, "... No moves left\n"); return;  }
	
	for (int i = 0; i < n && best && best->u.playouts >= 50; i++) {
		seq[i] = node_coord(best);
		best = u->policy->choose(u->policy, best, b, color, resign);
	}
	
	gogui_show_best_seq(stderr, b, color, seq, n);
}

/* GoGui live gfx: show best moves */
static void
uct_progress_gogui_best_moves(uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts)
{
	coord_t best_c[GOGUI_NBEST];
	float   best_r[GOGUI_NBEST];
	uct_get_best_moves(u, best_c, best_r, GOGUI_NBEST, false, 200);
	gogui_show_best_moves(stderr, b, color, best_c, best_r, GOGUI_NBEST);
}

/* GoGui live gfx: show winrates */
static void
uct_progress_gogui_winrates(uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts)
{
	coord_t best_c[GOGUI_NBEST];
	float   best_r[GOGUI_NBEST];
	uct_get_best_moves(u, best_c, best_r, GOGUI_NBEST, true, 200);

	int parity = (genmove_pondering(u) ? -1 : 1);
	for (int i = 0; i < GOGUI_NBEST; i++)
		best_r[i] = (parity == 1 ? best_r[i] : 1.0 - best_r[i]);
	
	gogui_show_winrates(stderr, b, color, best_c, best_r, GOGUI_NBEST);
}

static void
uct_progress_json(FILE *fh, uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts, coord_t *final, bool big)
{
	int parity = (genmove_pondering(u) ? -1 : 1);
	
	/* Prefix indicating JSON line. */
	fprintf(fh, "{\"%s\": {", final ? "move" : "frame");

	/* Plaout count */
	fprintf(fh, "\"playouts\": %d", playouts);

	/* Dynamic komi */
	if (t->use_extra_komi)
		fprintf(fh, ", \"extrakomi\": %.1f", t->extra_komi);

	if (final) {
		/* Final move choice */
		fprintf(fh, ", \"choice\": \"%s\"",
			coord2sstr(*final));
	} else {
		tree_node_t *best = u->policy->choose(u->policy, t->root, b, color, resign);
		if (best) {
			/* Best move */
			fprintf(fh, ", \"best\": {\"%s\": %f}",
				coord2sstr(best->coord),
				tree_node_get_value(t, parity, best->u.value));
		}
	}

	/* Best candidates */
	int cans = 20;
	tree_node_t *can[cans];
	memset(can, 0, sizeof(can));
	tree_node_t *best = t->root->children;
	while (best) {        /* XXX clean this up, use uct_get_best_moves() instead */
		int c = 0;
		while ((!can[c] || best->u.playouts > can[c]->u.playouts) && ++c < cans);
		for (int d = 0; d < c; d++) can[d] = can[d + 1];
		if (c > 0) can[c - 1] = best;
		best = best->sibling;
	}
	fprintf(fh, ", \"can\": [");
	bool first = true;
	while (--cans >= 0 && can[cans]) {
		/* Best sequence */
		if (first == true) {
			first = false;
			fprintf(fh, "[");
		} else {
			fprintf(fh, ", [");
		}
		best = can[cans];
		for (int depth = 0; depth < 20; depth++) {
			if (!best || best->u.playouts < 1) break;
			fprintf(fh, "%s{\"%s\": [%.3f, %i]}", depth > 0 ? "," : "",
				coord2sstr(best->coord),
				tree_node_get_value(t, parity, best->u.value),
				best->u.playouts);
			best = u->policy->choose(u->policy, best, b, color, resign);
		}
		fprintf(fh, "]");
	}
	fprintf(fh, "]");

	if (big) {
		/* Average score. */
		if (t->avg_score.playouts > 0)
			fprintf(fh, ", \"avg\": {\"score\": %.3f}", t->avg_score.value);
		/* Per-intersection information. */
		fprintf(fh, ", \"boards\": {");
		/* Position coloring information. */
		fprintf(fh, "\"colors\": [");
		int f = 0;
		foreach_point(b) {
			if (board_at(b, c) == S_OFFBOARD) continue;
			fprintf(fh, "%s%d", f++ > 0 ? "," : "", board_at(b, c));
		} foreach_point_end;
		fprintf(fh, "]");
		/* Ownership statistics. Value (0..1000) for each possible
		 * point describes likelihood of this point becoming black.
		 * Normally, white rate is 1000-value; exception are possible
		 * seki points, but these should be rare. */
		fprintf(fh, ", \"territory\": [");
		f = 0;
		foreach_point(b) {
			if (board_at(b, c) == S_OFFBOARD) continue;
			int rate = u->ownermap.map[c][S_BLACK] * 1000 / u->ownermap.playouts;
			fprintf(fh, "%s%d", f++ > 0 ? "," : "", rate);
		} foreach_point_end;
		fprintf(fh, "]");
		fprintf(fh, "}");
	}

	fprintf(fh, "}}\n");
}

static void
uct_progress_gogui_livegfx(uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts, coord_t *final)
{
	if (!gogui_livegfx)  return;

	/* GoGui reads live gfx commands on stderr. */
	fprintf(stderr, "gogui-gfx:\n");

	if      (gogui_livegfx == UR_GOGUI_BEST)  uct_progress_gogui_best_moves(u, t, b, color, playouts);
	else if (gogui_livegfx == UR_GOGUI_SEQ)   uct_progress_gogui_sequence(u, t, b, color, playouts);
	else if (gogui_livegfx == UR_GOGUI_WR)    uct_progress_gogui_winrates(u, t, b, color, playouts);
	else    assert(0);

	fprintf(stderr, "\n");
}

/* Print search progress in text / json / leela-zero format
 * Also takes care of gogui livegfx updates if enabled.
 * If @playouts is 0 show total playouts */
void
uct_progress_status(uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts, coord_t *final)
{
	if (!playouts)
		playouts = t->root->u.playouts;
		
	if      (u->reporting == UR_TEXT)        uct_progress_text(u->report_fh, u, t, b, color, playouts);
	else if (u->reporting == UR_JSON)        uct_progress_json(u->report_fh, u, t, b, color, playouts, final, false);
	else if (u->reporting == UR_JSON_BIG)    uct_progress_json(u->report_fh, u, t, b, color, playouts, final, true);
	else if (u->reporting == UR_LEELA_ZERO)  uct_progress_lz(u->report_fh, u, t, b, color);
	else    assert(0);
	
	uct_progress_gogui_livegfx(u, t, b, color, playouts, final);
}

static inline void
record_amaf_move(playout_amafmap_t *amaf, coord_t coord, bool is_ko_capture)
{
	assert(amaf->gamelen < MAX_GAMELEN);
	amaf->is_ko_capture[amaf->gamelen] = is_ko_capture;
	amaf->game[amaf->gamelen++] = coord;
}

static int
uct_leaf_node(uct_t *u, board_t *b, enum stone player_color,
              playout_amafmap_t *amaf,
	      uct_descent_t *descent, int *dlen,
	      tree_node_t *significant[2],
              tree_t *t, tree_node_t *n, enum stone node_color,
	      char *spaces)
{
	enum stone next_color = stone_other(node_color);
	int parity = (next_color == player_color ? 1 : -1);

	if (UDEBUGL(7))
		fprintf(stderr, "%s*-- UCT playout #%d start [%s] %f\n",
			spaces, n->u.playouts, coord2sstr(node_coord(n)),
			tree_node_get_value(t, -parity, n->u.value));

	playout_setup_t ps = playout_setup(u->gamelen, u->mercymin);
	int result = playout_play_game(&ps, b, next_color,
				       u->playout_amaf ? amaf : NULL,
				       &u->ownermap, u->playout);
	if (next_color == S_WHITE) {
		/* We need the result from black's perspective. */
		result = - result;
	}
	if (UDEBUGL(7))
		fprintf(stderr, "%s -- [%d..%d] %s random playout result %d\n",
		        spaces, player_color, next_color, coord2sstr(node_coord(n)), result);

	return result;
}

static floating_t
scale_value(uct_t *u, board_t *b, enum stone node_color, tree_node_t *significant[2], int result)
{
	floating_t rval = result > 0 ? 1.0 : result < 0 ? 0.0 : 0.5;
	if (u->val_scale && result != 0) {
		if (u->val_byavg) {
			if (u->t->avg_score.playouts < 50)
				return rval;
			result -= u->t->avg_score.value * 2;
		}

		double scale = u->val_scale;
		if (u->val_bytemp) {
			/* xvalue is 0 at 0.5, 1 at 0 or 1 */
			/* No correction for parity necessary. */
			double xvalue = significant[node_color - 1] ? fabs(significant[node_color - 1]->u.value - 0.5) * 2 : 0;
			scale = u->val_bytemp_min + (u->val_scale - u->val_bytemp_min) * xvalue;
		}

		int vp = u->val_points;
		if (!vp) {
			vp = board_stride(b) - 1; vp *= vp; vp *= 2;
		}

		floating_t sval = (floating_t) abs(result) / vp;
		sval = sval > 1 ? 1 : sval;
		if (result < 0) sval = 1 - sval;
		if (u->val_extra)
			rval += scale * sval;
		else
			rval = (1 - scale) * rval + scale * sval;
		// fprintf(stderr, "score %d => sval %f, rval %f\n", result, sval, rval);
	}
	return rval;
}

static tree_node_t *
uct_playout_descent(uct_t *u, board_t *b, enum stone player_color, tree_t *t, int *presult)
{
	playout_amafmap_t amaf;
	amaf.gamelen = amaf.game_baselen = 0;

	/* Walk the tree until we find a leaf, then expand it and do
	 * a random playout. */
	tree_node_t *n = t->root;
	enum stone node_color = stone_other(player_color);
	assert(node_color == t->root_color);

	/* Make sure root node is expanded. Normally that's the case,
	 * except direct calls to uct_playout() */
	if (tree_leaf_node(n) && !__sync_lock_test_and_set(&n->is_expanded, 1))
		tree_expand_node(t, n, b, player_color, u, 1);
	
	/* Tree descent history. */
	/* XXX: This is somewhat messy since @n and descent[dlen-1].node are
	 * redundant. */
	uct_descent_t descent[DESCENT_DLEN];
	descent[0].node = n;
	int dlen = 1;
	/* Total value of the sequence. */
	move_stats_t seq_value = move_stats(0.0, 0);
	/* The last "significant" node along the descent (i.e. node
	 * with higher than configured number of playouts). For black
	 * and white. */
	tree_node_t *significant[2] = { NULL, NULL };
	if (n->u.playouts >= u->significant_threshold)
		significant[node_color - 1] = n;

	int result;
	int passes = is_pass(last_move(b).coord) && b->moves > 0;

	/* debug */
	static char spaces[] = "\0                                                      ";
	/* /debug */
	if (UDEBUGL(8))
		fprintf(stderr, "--- (#%d) UCT walk with color %d\n", t->root->u.playouts, player_color);

	while (!tree_leaf_node(n) && passes < 2) {
		spaces[dlen - 1] = ' '; spaces[dlen] = 0;


		/*** Choose a node to descend to: */

		/* Parity is chosen already according to the child color, since
		 * it is applied to children. */
		node_color = stone_other(node_color);
		int parity = (node_color == player_color ? 1 : -1);

		assert(dlen < DESCENT_DLEN);
		descent[dlen] = descent[dlen - 1];

		if (!u->random_policy_chance || fast_random(u->random_policy_chance))
			u->policy->descend(u->policy, t, &descent[dlen], parity, u->allow_pass);
		else
			u->random_policy->descend(u->random_policy, t, &descent[dlen], parity, u->allow_pass);


		/*** Perform the descent: */

		if (descent[dlen].node->u.playouts >= u->significant_threshold)
			significant[node_color - 1] = descent[dlen].node;

		seq_value.playouts += descent[dlen].value.playouts;
		seq_value.value += descent[dlen].value.value * descent[dlen].value.playouts;
		n = descent[dlen++].node;
		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "%s+-- UCT sent us to [%s:%d] %d,%f\n",
			        spaces, coord2sstr(node_coord(n)),
				node_coord(n), n->u.playouts,
				tree_node_get_value(t, parity, n->u.value));

		if (u->virtual_loss)
			__sync_fetch_and_add(&n->descents, u->virtual_loss);

		move_t m = { node_coord(n), node_color };
		int res = board_play(b, &m);

		if (res < 0 || (!is_pass(m.coord) && !group_at(b, m.coord)) /* suicide */
		    || b->superko_violation) {
			if (UDEBUGL(4)) {
				for (tree_node_t *ni = n; ni; ni = ni->parent)
					fprintf(stderr, "%s<%" PRIhash "> ", coord2sstr(node_coord(ni)), ni->hash);
				fprintf(stderr, "marking invalid %s node %d,%d res %d group %d spk %d\n",
				        stone2str(node_color), coord_x(node_coord(n)), coord_y(node_coord(n)),
					res, group_at(b, m.coord), b->superko_violation);
			}
			n->hints |= TREE_HINT_INVALID;
			*presult = 0;
			return n;
		}

		assert(node_coord(n) >= -1);
		record_amaf_move(&amaf, node_coord(n), board_playing_ko_threat(b));

		if (is_pass(node_coord(n)))  passes++;
		else                         passes = 0;

		enum stone next_color = stone_other(node_color);
		/* We need to make sure only one thread expands the node. If
		 * we are unlucky enough for two threads to meet in the same
		 * node, the latter one will simply do another simulation from
		 * the node itself, no big deal. t->nodes_size may exceed
		 * the maximum in multi-threaded case but not by much so it's ok.
		 * The size test must be before the test&set not after, to allow
		 * expansion of the node later if enough nodes have been freed. */
		if (tree_leaf_node(n)
		    && n->u.playouts - u->virtual_loss >= u->expand_p && t->nodes_size < t->max_tree_size
		    && !__sync_lock_test_and_set(&n->is_expanded, 1))
			tree_expand_node(t, n, b, next_color, u, -parity);
	}

	amaf.game_baselen = amaf.gamelen;

	if (t->use_extra_komi && u->dynkomi->persim)
		b->komi += round(u->dynkomi->persim(u->dynkomi, b, t, n));

	/* !!! !!! !!!
	 * ALERT: The "result" number is extremely confusing. In some parts
	 * of the code, it is from white's perspective, but here positive
	 * number is black's win! Be VERY CAREFUL.
	 * !!! !!! !!! */

	// assert(tree_leaf_node(n));
	/* In case of parallel tree search, the assertion might
	 * not hold if two threads chew on the same node. */
	result = uct_leaf_node(u, b, player_color, &amaf, descent, &dlen, significant, t, n, node_color, spaces);

	if (u->policy->wants_amaf && u->playout_amaf_cutoff) {
		unsigned int cutoff = amaf.game_baselen;
		cutoff += (amaf.gamelen - amaf.game_baselen) * u->playout_amaf_cutoff / 100;
		amaf.gamelen = cutoff;
	}

	/* Record the result. */

	assert(n == t->root || n->parent);
	floating_t rval = scale_value(u, b, node_color, significant, result);
	u->policy->update(u->policy, t, n, node_color, player_color, &amaf, b, rval);

	stats_add_result(&t->avg_score, (float)result / 2, 1);
	if (t->use_extra_komi) {
		stats_add_result(&u->dynkomi->score, (float)result / 2, 1);
		stats_add_result(&u->dynkomi->value, rval, 1);
	}

	*presult = result;
	return n;
}

int
uct_playout(uct_t *u, board_t *b, enum stone player_color, tree_t *t)
{
	board_t b2;
	board_copy(&b2, b);
	
	int result;
	tree_node_t *n = uct_playout_descent(u, &b2, player_color, t, &result);
	
	/* We need to undo the virtual loss we added during descend. */
	if (u->virtual_loss) {
		for (; n->parent; n = n->parent) {
			__sync_fetch_and_sub(&n->descents, u->virtual_loss);
		}
	}

	board_done(&b2);
	return result;
}

int
uct_playouts(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti)
{
	int i;
	for (i = 0; !uct_halt; i++)
		uct_playout(u, b, color, t);
	return i;
}
