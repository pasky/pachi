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
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"
#include "uct/prior.h"
#include "gogui.h"

#define DESCENT_DLEN 512


void
uct_progress_text(uct_t *u, tree_t *t, enum stone color, int playouts)
{
	board_t *b = t->board;
	if (!UDEBUGL(0))
		return;

	/* Best move */
	tree_node_t *best = u->policy->choose(u->policy, t->root, b, color, resign);
	if (!best) {
		fprintf(stderr, "... No moves left\n");
		return;
	}
	fprintf(stderr, "[%d] ", playouts);
	fprintf(stderr, "best %.1f%% ", 100 * tree_node_get_value(t, 1, best->u.value));

	/* Dynamic komi */
	if (t->use_extra_komi)
		fprintf(stderr, "xkomi %.1f ", t->extra_komi);

	/* Best sequence */
	fprintf(stderr, "| seq ");
	for (int depth = 0; depth < 4; depth++) {
		if (best && best->u.playouts >= 25) {
			fprintf(stderr, "%3s ", coord2sstr(node_coord(best)));
			best = u->policy->choose(u->policy, best, b, color, resign);
		} else {
			fprintf(stderr, "    ");
		}
	}

	/* Best candidates */
	int nbest = 4;
	float   best_r[nbest];
	coord_t best_c[nbest];
	uct_get_best_moves(u, best_c, best_r, nbest, true);

	fprintf(stderr, "| can %c ", color == S_BLACK ? 'b' : 'w');
	for (int i = 0; i < nbest; i++)
		if (!is_pass(best_c[i]))
			fprintf(stderr, "%3s(%.1f) ", coord2sstr(best_c[i]), 100 * best_r[i]);
		else
			fprintf(stderr, "          ");

	/* Tree memory usage */
	if (UDEBUGL(3))
		fprintf(stderr, " | %.1fMb", (float)t->nodes_size / 1024 / 1024);
	
	fprintf(stderr, "\n");
}

static void
uct_progress_leelaz(uct_t *u, tree_t *t, enum stone color)
{
	board_t *b = t->board;
	tree_node_t *node = u->t->root;

	/* Best candidates */
	int nbest = 20;
	float   best_pl[nbest];	
	float   best_wr[nbest];
	coord_t best_c[nbest];
	uct_get_best_moves_at(u, node, best_c, best_pl, nbest, false);
	uct_get_best_moves_at(u, node, best_c, best_wr, nbest, true);

	/* Priors */
	float   best_pr[19 * 19];
	coord_t best_cpr[19 * 19];
	float   priors[BOARD_MAX_COORDS];  memset(priors, 0, sizeof(priors));
	get_node_prior_best_moves(node, best_cpr, best_pr, 19 * 19);
	for (int i = 0; i < 19 * 19; i++)
		priors[best_cpr[i]] = best_pr[i];

	// Leela-Zero format:
	// info move Q16 visits 1 winrate 4687 prior 2198 order 0 pv Q16 [...]
	for (int i = 0; i < nbest && !is_pass(best_c[i]); i++) {
		if (best_pl[i] < 500)  break;  // too few playouts
		fprintf(stderr, "info move %s visits %i winrate %i prior %i order %i ",
			coord2sstr(best_c[i]), (int)best_pl[i], (int)(best_wr[i] * 10000),
			(int)(priors[best_c[i]] * 10000), i);

		/* Dump best variation */
		fprintf(stderr, "pv %s ", coord2sstr(best_c[i]));
		tree_node_t *n = tree_get_node(node, best_c[i]);
		while (1) {
			n = u->policy->choose(u->policy, n, b, color, resign);
			if (!n || n->u.playouts < 100) break;
			fprintf(stderr, "%s ", coord2sstr(node_coord(n)));
		}
	}
	fprintf(stderr, "\n");
}

/* GoGui live gfx: show best sequence */
static void
uct_progress_gogui_sequence(strbuf_t *buf, uct_t *u, tree_t *t, enum stone color, int playouts)
{
	coord_t seq[4] = { pass, pass, pass, pass };

	/* Best move */
	tree_node_t *best = u->policy->choose(u->policy, t->root, t->board, color, resign);
	if (!best) {  fprintf(stderr, "... No moves left\n"); return;  }
	
	for (int i = 0; i < 4 && best && best->u.playouts >= 25; i++) {
		seq[i] = node_coord(best);
		best = u->policy->choose(u->policy, best, t->board, color, resign);
	}
	
	gogui_show_best_seq(buf, t->board, color, seq, 4);
}

/* GoGui live gfx: show best moves */
static void
uct_progress_gogui_best(strbuf_t *buf, uct_t *u, tree_t *t, enum stone color, int playouts)
{
	coord_t best_c[GOGUI_CANDIDATES];
	float   best_r[GOGUI_CANDIDATES];
	uct_get_best_moves(u, best_c, best_r, GOGUI_CANDIDATES, false);
	gogui_show_best_moves(buf, t->board, color, best_c, best_r, GOGUI_CANDIDATES);
}

/* GoGui live gfx: show winrates */
static void
uct_progress_gogui_winrates(strbuf_t *buf, uct_t *u, tree_t *t, enum stone color, int playouts)
{
	coord_t best_c[GOGUI_CANDIDATES];
	float   best_r[GOGUI_CANDIDATES];
	uct_get_best_moves(u, best_c, best_r, GOGUI_CANDIDATES, true);
	gogui_show_winrates(buf, t->board, color, best_c, best_r, GOGUI_CANDIDATES);
}

void
uct_progress_json(uct_t *u, tree_t *t, enum stone color, int playouts, coord_t *final, bool big)
{
	/* Prefix indicating JSON line. */
	fprintf(stderr, "{\"%s\": {", final ? "move" : "frame");

	/* Plaout count */
	fprintf(stderr, "\"playouts\": %d", playouts);

	/* Dynamic komi */
	if (t->use_extra_komi)
		fprintf(stderr, ", \"extrakomi\": %.1f", t->extra_komi);

	if (final) {
		/* Final move choice */
		fprintf(stderr, ", \"choice\": \"%s\"",
			coord2sstr(*final));
	} else {
		tree_node_t *best = u->policy->choose(u->policy, t->root, t->board, color, resign);
		if (best) {
			/* Best move */
			fprintf(stderr, ", \"best\": {\"%s\": %f}",
				coord2sstr(best->coord),
				tree_node_get_value(t, 1, best->u.value));
		}
	}

	/* Best candidates */
	int cans = 4;
	tree_node_t *can[cans];
	memset(can, 0, sizeof(can));
	tree_node_t *best = t->root->children;
	while (best) {
		int c = 0;
		while ((!can[c] || best->u.playouts > can[c]->u.playouts) && ++c < cans);
		for (int d = 0; d < c; d++) can[d] = can[d + 1];
		if (c > 0) can[c - 1] = best;
		best = best->sibling;
	}
	fprintf(stderr, ", \"can\": [");
	while (--cans >= 0) {
		if (!can[cans]) break;
		/* Best sequence */
		fprintf(stderr, "[");
		best = can[cans];
		for (int depth = 0; depth < 4; depth++) {
			if (!best || best->u.playouts < 25) break;
			fprintf(stderr, "%s{\"%s\": [%.3f, %i]}", depth > 0 ? "," : "",
				coord2sstr(best->coord),
				tree_node_get_value(t, 1, best->u.value),
				best->u.playouts);
			best = u->policy->choose(u->policy, best, t->board, color, resign);
		}
		fprintf(stderr, "]%s", cans > 0 ? ", " : "");
	}
	fprintf(stderr, "]");

	if (big) {
		/* Average score. */
		if (t->avg_score.playouts > 0)
			fprintf(stderr, ", \"avg\": {\"score\": %.3f}", t->avg_score.value);
		/* Per-intersection information. */
		fprintf(stderr, ", \"boards\": {");
		/* Position coloring information. */
		fprintf(stderr, "\"colors\": [");
		int f = 0;
		foreach_point(t->board) {
			if (board_at(t->board, c) == S_OFFBOARD) continue;
			fprintf(stderr, "%s%d", f++ > 0 ? "," : "", board_at(t->board, c));
		} foreach_point_end;
		fprintf(stderr, "]");
		/* Ownership statistics. Value (0..1000) for each possible
		 * point describes likelihood of this point becoming black.
		 * Normally, white rate is 1000-value; exception are possible
		 * seki points, but these should be rare. */
		fprintf(stderr, ", \"territory\": [");
		f = 0;
		foreach_point(t->board) {
			if (board_at(t->board, c) == S_OFFBOARD) continue;
			int rate = u->ownermap.map[c][S_BLACK] * 1000 / u->ownermap.playouts;
			fprintf(stderr, "%s%d", f++ > 0 ? "," : "", rate);
		} foreach_point_end;
		fprintf(stderr, "]");
		fprintf(stderr, "}");
	}

	fprintf(stderr, "}}\n");
}

void
uct_progress_status(uct_t *u, tree_t *t, enum stone color, int playouts, coord_t *final)
{
	switch (u->reporting) {
		case UR_TEXT:
			uct_progress_text(u, t, color, playouts);
			break;
		case UR_JSON:
		case UR_JSON_BIG:
			uct_progress_json(u, t, color, playouts, final,
			                  u->reporting == UR_JSON_BIG);
		case UR_LEELAZ:
			uct_progress_leelaz(u, t, color);
			break;
		default: assert(0);
	}

	if (!gogui_livegfx)
		return;

	char buffer[10000];  strbuf_t strbuf;
	strbuf_t *buf = strbuf_init(&strbuf, buffer, sizeof(buffer));

	switch(gogui_livegfx) {
		case UR_GOGUI_BEST:
			uct_progress_gogui_best(buf, u, t, color, playouts);
			break;
		case UR_GOGUI_SEQ:
			uct_progress_gogui_sequence(buf, u, t, color, playouts);
			break;
		case UR_GOGUI_WR:
			uct_progress_gogui_winrates(buf, u, t, color, playouts);
			break;
		default: assert(0);
	}

	gogui_show_livegfx(buf->str);
}

static inline void
record_amaf_move(playout_amafmap_t *amaf, coord_t coord, bool is_ko_capture)
{
	assert(amaf->gamelen < MAX_GAMELEN);
	amaf->is_ko_capture[amaf->gamelen] = is_ko_capture;
	amaf->game[amaf->gamelen++] = coord;
}


typedef struct {
	uct_t *uct;
	tree_t *tree;
	tree_node_t *lnode;
} uct_playout_callback_t;


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
			vp = board_size(b) - 1; vp *= vp; vp *= 2;
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

static double
local_value(uct_t *u, board_t *b, coord_t coord, enum stone color)
{
	/* Tactical evaluation of move @coord by color @color, given
	 * simulation end position @b. I.e., a move is tactically good
	 * if the resulting group stays on board until the game end. */
	/* We can also take into account surrounding stones, e.g. to
	 * encourage taking off external liberties during a semeai. */
	double val = board_local_value(u->local_tree_neival, b, coord, color);
	return (color == S_WHITE) ? 1.f - val : val;
}

static void
record_local_sequence(uct_t *u, tree_t *t, board_t *endb,
                      uct_descent_t *descent, int dlen, int di,
		      enum stone seq_color)
{
#define LTREE_DEBUG if (UDEBUGL(6))

	/* Ignore pass sequences. */
	if (is_pass(node_coord(descent[di].node)))
		return;

	LTREE_DEBUG board_print(endb, stderr);
	LTREE_DEBUG fprintf(stderr, "recording local %s sequence: ",
		stone2str(seq_color));

	/* Sequences starting deeper are less relevant in general. */
	int pval = LTREE_PLAYOUTS_MULTIPLIER;
	if (u->local_tree && u->local_tree_depth_decay > 0)
		pval = ((floating_t) pval) / pow(u->local_tree_depth_decay, di - 1);
	if (!pval) {
		LTREE_DEBUG fprintf(stderr, "too deep @%d\n", di);
		return;
	}

	/* Pick the right local tree root... */
	tree_node_t *lnode = seq_color == S_BLACK ? t->ltree_black : t->ltree_white;
	lnode->u.playouts++;

	/* ...determine the sequence value... */
	double sval = 0.5;
	if (u->local_tree_eval != LTE_EACH) {
		sval = local_value(u, endb, node_coord(descent[di].node), seq_color);
		LTREE_DEBUG fprintf(stderr, "(goal %s[%s %1.3f][%d]) ",
			coord2sstr(node_coord(descent[di].node)),
			stone2str(seq_color), sval, descent[di].node->d);

		if (u->local_tree_eval == LTE_TOTAL) {
			int di0 = di;
			while (di < dlen && (di == di0 || descent[di].node->d < u->tenuki_d)) {
				enum stone color = (di - di0) % 2 ? stone_other(seq_color) : seq_color;
				double rval = local_value(u, endb, node_coord(descent[di].node), color);
				if ((di - di0) % 2)
					rval = 1 - rval;
				sval += rval;
				di++;
			}
			sval /= (di - di0 + 1);
			di = di0;
		}
	}

	/* ...and record the sequence. */
	int di0 = di;
	while (di < dlen && !is_pass(node_coord(descent[di].node))
	       && (di == di0 || descent[di].node->d < u->tenuki_d)) {
		enum stone color = (di - di0) % 2 ? stone_other(seq_color) : seq_color;
		double rval;
		if (u->local_tree_eval != LTE_EACH)
			rval = sval;
		else
			rval = local_value(u, endb, node_coord(descent[di].node), color);
		LTREE_DEBUG fprintf(stderr, "%s[%s %1.3f][%d] ",
			coord2sstr(node_coord(descent[di].node)),
			stone2str(color), rval, descent[di].node->d);
		lnode = tree_get_node2(t, lnode, node_coord(descent[di++].node), true);
		assert(lnode);
		stats_add_result(&lnode->u, rval, pval);
	}

	/* Add lnode for tenuki (pass) if we descended further. */
	if (di < dlen) {
		double rval = u->local_tree_eval != LTE_EACH ? sval : 0.5;
		LTREE_DEBUG fprintf(stderr, "pass ");
		lnode = tree_get_node2(t, lnode, pass, true);
		assert(lnode);
		stats_add_result(&lnode->u, rval, pval);
	}
	
	LTREE_DEBUG fprintf(stderr, "\n");
}

static tree_node_t *
uct_playout_descent(uct_t *u, board_t *b, board_t *b2, enum stone player_color, tree_t *t, int *presult)
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
	descent[0].node = n; descent[0].lnode = NULL;
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
	int pass_limit = real_board_size2(b2) / 2;
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
		if (u->local_tree && (!descent[dlen].lnode || descent[dlen].node->d >= u->tenuki_d)) {
			/* Start new local sequence. */
			/* Remember that node_color already holds color of the
			 * to-be-found child. */
			descent[dlen].lnode = node_color == S_BLACK ? t->ltree_black : t->ltree_white;
		}

		if (!u->random_policy_chance || fast_random(u->random_policy_chance))
			u->policy->descend(u->policy, t, &descent[dlen], parity, (b2->moves > pass_limit));
		else
			u->random_policy->descend(u->random_policy, t, &descent[dlen], parity, (b2->moves > pass_limit));


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
		int res = board_play(b2, &m);

		if (res < 0 || (!is_pass(m.coord) && !group_at(b2, m.coord)) /* suicide */
		    || b2->superko_violation) {
			if (UDEBUGL(4)) {
				for (tree_node_t *ni = n; ni; ni = ni->parent)
					fprintf(stderr, "%s<%" PRIhash "> ", coord2sstr(node_coord(ni)), ni->hash);
				fprintf(stderr, "marking invalid %s node %d,%d res %d group %d spk %d\n",
				        stone2str(node_color), coord_x(node_coord(n)), coord_y(node_coord(n)),
					res, group_at(b2, m.coord), b2->superko_violation);
			}
			n->hints |= TREE_HINT_INVALID;
			*presult = 0;
			return n;
		}

		assert(node_coord(n) >= -1);
		record_amaf_move(&amaf, node_coord(n), board_playing_ko_threat(b2));

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
		    && n->u.playouts - u->virtual_loss >= u->expand_p && t->nodes_size < u->max_tree_size
		    && !__sync_lock_test_and_set(&n->is_expanded, 1))
			tree_expand_node(t, n, b2, next_color, u, -parity);
	}

	amaf.game_baselen = amaf.gamelen;

	if (t->use_extra_komi && u->dynkomi->persim)
		b2->komi += round(u->dynkomi->persim(u->dynkomi, b2, t, n));

	/* !!! !!! !!!
	 * ALERT: The "result" number is extremely confusing. In some parts
	 * of the code, it is from white's perspective, but here positive
	 * number is black's win! Be VERY CAREFUL.
	 * !!! !!! !!! */

	// assert(tree_leaf_node(n));
	/* In case of parallel tree search, the assertion might
	 * not hold if two threads chew on the same node. */
	result = uct_leaf_node(u, b2, player_color, &amaf, descent, &dlen, significant, t, n, node_color, spaces);

	if (u->policy->wants_amaf && u->playout_amaf_cutoff) {
		unsigned int cutoff = amaf.game_baselen;
		cutoff += (amaf.gamelen - amaf.game_baselen) * u->playout_amaf_cutoff / 100;
		amaf.gamelen = cutoff;
	}

	/* Record the result. */

	assert(n == t->root || n->parent);
	floating_t rval = scale_value(u, b, node_color, significant, result);
	u->policy->update(u->policy, t, n, node_color, player_color, &amaf, b2, rval);

	stats_add_result(&t->avg_score, (float)result / 2, 1);
	if (t->use_extra_komi) {
		stats_add_result(&u->dynkomi->score, (float)result / 2, 1);
		stats_add_result(&u->dynkomi->value, rval, 1);
	}

	if (u->local_tree && n->parent && !is_pass(node_coord(n)) && dlen > 0) {
		/* Get the local sequences and record them in ltree. */
		/* We will look for sequence starts in our descent
		 * history, then run record_local_sequence() for each
		 * found sequence start; record_local_sequence() may
		 * pick longer sequences from descent history then,
		 * which is expected as it will create new lnodes. */
		enum stone seq_color = player_color;
		/* First move always starts a sequence. */
		record_local_sequence(u, t, b2, descent, dlen, 1, seq_color);
		seq_color = stone_other(seq_color);
		for (int dseqi = 2; dseqi < dlen; dseqi++, seq_color = stone_other(seq_color)) {
			if (u->local_tree_allseq) {
				/* We are configured to record all subsequences. */
				record_local_sequence(u, t, b2, descent, dlen, dseqi, seq_color);
				continue;
			}
			if (descent[dseqi].node->d >= u->tenuki_d) {
				/* Tenuki! Record the fresh sequence. */
				record_local_sequence(u, t, b2, descent, dlen, dseqi, seq_color);
				continue;
			}
			if (descent[dseqi].lnode && !descent[dseqi].lnode) {
				/* Record result for in-descent picked sequence. */
				record_local_sequence(u, t, b2, descent, dlen, dseqi, seq_color);
				continue;
			}
		}
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
	tree_node_t *n = uct_playout_descent(u, b, &b2, player_color, t, &result);
	
	/* We need to undo the virtual loss we added during descend. */
	if (u->virtual_loss) {
		for (; n->parent; n = n->parent) {
			__sync_fetch_and_sub(&n->descents, u->virtual_loss);
		}
	}

	board_done_noalloc(&b2);
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
