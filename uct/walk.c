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
#include "probdist.h"
#include "random.h"
#include "uct/dynkomi.h"
#include "uct/internal.h"
#include "uct/search.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"

#define DESCENT_DLEN 512


void
uct_progress_text(struct uct *u, struct tree *t, enum stone color, int playouts, bool final)
{
	if (!UDEBUGL(0))
		return;

	/* Best move */
	struct tree_node *best = u->policy->choose(u->policy, t->root, t->board, color, resign);
	if (!best) {
		fprintf(stderr, "... No moves left\n");
		return;
	}
	fprintf(stderr, "[%d] ", playouts);
	fprintf(stderr, "best %f ", tree_node_get_value(t, 1, best->u.value));

	/* Dynamic komi */
	if (t->use_extra_komi)
		fprintf(stderr, "komi %.1f ", t->extra_komi);

	/* Best sequence */
	fprintf(stderr, "| seq ");
	for (int depth = 0; depth < 4; depth++) {
		if (best && best->u.playouts >= 25) {
			fprintf(stderr, "%3s ", coord2sstr(best->coord, t->board));
			best = u->policy->choose(u->policy, best, t->board, color, resign);
		} else {
			fprintf(stderr, "    ");
		}
	}

	/* Best candidates */
	fprintf(stderr, "| can ");
	int cans = 4;
	struct tree_node *can[cans];
	memset(can, 0, sizeof(can));
	best = t->root->children;
	while (best) {
		int c = 0;
		while ((!can[c] || best->u.playouts > can[c]->u.playouts) && ++c < cans);
		for (int d = 0; d < c; d++) can[d] = can[d + 1];
		if (c > 0) can[c - 1] = best;
		best = best->sibling;
	}
	while (--cans >= 0) {
		if (can[cans]) {
			fprintf(stderr, "%3s(%.3f) ",
			        coord2sstr(can[cans]->coord, t->board),
				tree_node_get_value(t, 1, can[cans]->u.value));
		} else {
			fprintf(stderr, "           ");
		}
	}

	fprintf(stderr, "\n");
}

void
uct_progress_json(struct uct *u, struct tree *t, enum stone color, int playouts, bool final, bool big)
{
	/* Prefix indicating JSON line. */
	fprintf(stderr, "{\"%s\": {", final ? "move" : "frame");

	/* Plaout count */
	fprintf(stderr, "\"playouts\": %d", playouts);

	/* Dynamic komi */
	if (t->use_extra_komi)
		fprintf(stderr, ", \"extrakomi\": %.1f", t->extra_komi);

	struct tree_node *best = u->policy->choose(u->policy, t->root, t->board, color, resign);
	if (best) {
		/* Best move */
		fprintf(stderr, ", \"best\": {\"%s\": %f}",
			coord2sstr(best->coord, t->board),
			tree_node_get_value(t, 1, best->u.value));

		/* Best sequence */
		fprintf(stderr, ", \"seq\": [");
		for (int depth = 0; depth < 4; depth++) {
			if (!best || best->u.playouts < 25) break;
			fprintf(stderr, "%s\"%s\"", depth > 0 ? "," : "",
				coord2sstr(best->coord, t->board));
			best = u->policy->choose(u->policy, best, t->board, color, resign);
		}
		fprintf(stderr, "]");
	}

	/* Best candidates */
	int cans = 4;
	struct tree_node *can[cans];
	memset(can, 0, sizeof(can));
	best = t->root->children;
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
		fprintf(stderr, "%s{\"%s\":%.3f}",
			cans < 3 ? "," : "",
			coord2sstr(can[cans]->coord, t->board),
			tree_node_get_value(t, 1, can[cans]->u.value));
	}
	fprintf(stderr, "]");

	if (big) {
		/* Ownership statistics. {"b":val,"w":val,"d":val}
		 * where each val is in range [0,1] describes likelihood
		 * of this point becoming black, white and dame.
		 * If dame rate would be 0, only black rate is sent and
		 * white rate can be computed as 1-blackrate. */
		fprintf(stderr, ", \"territstatus\": [");
		int f = 0;
		foreach_point(t->board) {
			if (board_at(t->board, c) != S_NONE) continue;
			fprintf(stderr, "%s{\"%s\":{", f++ > 0 ? "," : "",
				coord2sstr(c, t->board));
			floating_t drate = (floating_t) u->ownermap.map[c][S_NONE] / u->ownermap.playouts;
			bool p = false;
#define print_rate(color,letter) \
			if (drate >= 0.001 || color == S_BLACK) { \
				floating_t rate = (floating_t) u->ownermap.map[c][color] / u->ownermap.playouts; \
				fprintf(stderr, "%s\"%c\":%.3f", p ? "," : "", letter, rate); \
				p = true; \
			}
			print_rate(S_BLACK, 'b');
			print_rate(S_WHITE, 'w');
			print_rate(S_NONE, 'd');
			fprintf(stderr, "}}");
		} foreach_point_end;
		fprintf(stderr, "]");

		/* Chain status statistics. Each chain is identified
		 * by some stone within, and bound to a value in range
		 * [0,1] describing likelihood of survival. */
		fprintf(stderr, ", \"chainstatus\": [");
		f = 0;
		foreach_point(t->board) {
			group_t g = group_at(t->board, c);
			if (!g || groupnext_at(t->board, c)) continue;
			/* Last stone in some group. */
			fprintf(stderr, "%s{\"%s\":%.3f}",
				f++ > 0 ? "," : "",
				coord2sstr(c, t->board),
				(floating_t) u->ownermap.map[c][board_at(t->board, c)] / u->ownermap.playouts);
		} foreach_point_end;
		fprintf(stderr, "]");
	}

	fprintf(stderr, "}}\n");
}

void
uct_progress_status(struct uct *u, struct tree *t, enum stone color, int playouts, bool final)
{
	switch (u->reporting) {
		case UR_TEXT:
			uct_progress_text(u, t, color, playouts, final);
			break;
		case UR_JSON:
		case UR_JSON_BIG:
			uct_progress_json(u, t, color, playouts, final,
			                  u->reporting == UR_JSON_BIG);
			break;
		default: assert(0);
	}
}


static void
record_amaf_move(struct playout_amafmap *amaf, coord_t coord, enum stone color)
{
	if (amaf->map[coord] == S_NONE || amaf->map[coord] == color) {
		amaf->map[coord] = color;
	} else { // XXX: Respect amaf->record_nakade
		amaf_op(amaf->map[coord], +);
	}
	amaf->game[amaf->gamelen].coord = coord;
	amaf->game[amaf->gamelen].color = color;
	amaf->gamelen++;
	assert(amaf->gamelen < sizeof(amaf->game) / sizeof(amaf->game[0]));
}


struct uct_playout_callback {
	struct uct *uct;
	struct tree *tree;
	struct tree_node *lnode;
};


static coord_t
uct_playout_hook(struct playout_policy *playout, struct playout_setup *setup, struct board *b, enum stone color, int mode)
{
	/* XXX: This is used in some non-master branches. */
	return pass;
}

static coord_t
uct_playout_prepolicy(struct playout_policy *playout, struct playout_setup *setup, struct board *b, enum stone color)
{
	return uct_playout_hook(playout, setup, b, color, 0);
}

static coord_t
uct_playout_postpolicy(struct playout_policy *playout, struct playout_setup *setup, struct board *b, enum stone color)
{
	return uct_playout_hook(playout, setup, b, color, 1);
}


static int
uct_leaf_node(struct uct *u, struct board *b, enum stone player_color,
              struct playout_amafmap *amaf,
	      struct uct_descent *descent, int *dlen,
	      struct tree_node *significant[2],
              struct tree *t, struct tree_node *n, enum stone node_color,
	      char *spaces)
{
	enum stone next_color = stone_other(node_color);
	int parity = (next_color == player_color ? 1 : -1);

	/* We need to make sure only one thread expands the node. If
	 * we are unlucky enough for two threads to meet in the same
	 * node, the latter one will simply do another simulation from
	 * the node itself, no big deal. t->nodes_size may exceed
	 * the maximum in multi-threaded case but not by much so it's ok.
	 * The size test must be before the test&set not after, to allow
	 * expansion of the node later if enough nodes have been freed. */
	if (n->u.playouts >= u->expand_p && t->nodes_size < u->max_tree_size
	    && !__sync_lock_test_and_set(&n->is_expanded, 1)) {
		tree_expand_node(t, n, b, next_color, u, parity);
        }
	if (UDEBUGL(7))
		fprintf(stderr, "%s*-- UCT playout #%d start [%s] %f\n",
			spaces, n->u.playouts, coord2sstr(n->coord, t->board),
			tree_node_get_value(t, parity, n->u.value));

	struct uct_playout_callback upc = {
		.uct = u,
		.tree = t,
		/* TODO: Don't necessarily restart the sequence walk when
		 * entering playout. */
		.lnode = NULL,
	};

	struct playout_setup ps = {
		.gamelen = u->gamelen,
		.mercymin = u->mercymin,
		.prepolicy_hook = uct_playout_prepolicy,
		.postpolicy_hook = uct_playout_postpolicy,
		.hook_data = &upc,
	};
	int result = play_random_game(&ps, b, next_color,
	                              u->playout_amaf ? amaf : NULL,
				      &u->ownermap, u->playout);
	if (next_color == S_WHITE) {
		/* We need the result from black's perspective. */
		result = - result;
	}
	if (UDEBUGL(7))
		fprintf(stderr, "%s -- [%d..%d] %s random playout result %d\n",
		        spaces, player_color, next_color, coord2sstr(n->coord, t->board), result);

	return result;
}

static floating_t
scale_value(struct uct *u, struct board *b, int result)
{
	floating_t rval = result > 0 ? 1.0 : result < 0 ? 0.0 : 0.5;
	if (u->val_scale && result != 0) {
		int vp = u->val_points;
		if (!vp) {
			vp = board_size(b) - 1; vp *= vp; vp *= 2;
		}

		floating_t sval = (floating_t) abs(result) / vp;
		sval = sval > 1 ? 1 : sval;
		if (result < 0) sval = 1 - sval;
		if (u->val_extra)
			rval += u->val_scale * sval;
		else
			rval = (1 - u->val_scale) * rval + u->val_scale * sval;
		// fprintf(stderr, "score %d => sval %f, rval %f\n", result, sval, rval);
	}
	return rval;
}

static void
record_local_sequence(struct uct *u, struct tree *t,
                      struct uct_descent *descent, int dlen, int di,
		      enum stone seq_color, floating_t rval)
{
#define LTREE_DEBUG if (UDEBUGL(6))

	/* Ignore pass sequences. */
	if (is_pass(descent[di].node->coord))
		return;

	/* Transform the rval appropriately, based on the expected
	 * result at the root of the sequence. */
	if (u->local_tree_rootseqval) {
		float expval = descent[di - 1].value.value;
		rval = stats_temper_value(rval, expval, u->local_tree);
	}

	LTREE_DEBUG fprintf(stderr, "recording result %f in local %s sequence: ",
		rval, stone2str(seq_color));

	/* Sequences starting deeper are less relevant in general. */
	int pval = LTREE_PLAYOUTS_MULTIPLIER;
	if (u->local_tree && u->local_tree_depth_decay > 0)
		pval = ((floating_t) pval) / pow(u->local_tree_depth_decay, di - 1);
	if (!pval) {
		LTREE_DEBUG fprintf(stderr, "too deep @%d\n", di);
		return;
	}

	/* Pick the right local tree root... */
	struct tree_node *lnode = seq_color == S_BLACK ? t->ltree_black : t->ltree_white;
	lnode->u.playouts++;

	/* ...and record the sequence. */
	int di0 = di;
	while (di < dlen && (di == di0 || descent[di].node->d < u->tenuki_d)) {
		LTREE_DEBUG fprintf(stderr, "%s[%d] ",
			coord2sstr(descent[di].node->coord, t->board),
			descent[di].node->d);
		lnode = tree_get_node(t, lnode, descent[di++].node->coord, true);
		assert(lnode);
		stats_add_result(&lnode->u, rval, pval);
	}

	/* Add lnode for tenuki (pass) if we descended further. */
	if (di < dlen) {
		LTREE_DEBUG fprintf(stderr, "pass ");
		lnode = tree_get_node(t, lnode, pass, true);
		assert(lnode);
		stats_add_result(&lnode->u, rval, pval);
	}
	
	LTREE_DEBUG fprintf(stderr, "\n");
}


int
uct_playout(struct uct *u, struct board *b, enum stone player_color, struct tree *t)
{
	struct board b2;
	board_copy(&b2, b);

	struct playout_amafmap *amaf = NULL;
	if (u->policy->wants_amaf) {
		amaf = calloc2(1, sizeof(*amaf));
		amaf->map = calloc2(board_size2(&b2) + 1, sizeof(*amaf->map));
		amaf->map++; // -1 is pass
	}

	/* Walk the tree until we find a leaf, then expand it and do
	 * a random playout. */
	struct tree_node *n = t->root;
	enum stone node_color = stone_other(player_color);
	assert(node_color == t->root_color);

	/* Tree descent history. */
	/* XXX: This is somewhat messy since @n and descent[dlen-1].node are
	 * redundant. */
	struct uct_descent descent[DESCENT_DLEN];
	descent[0].node = n; descent[0].lnode = NULL;
	int dlen = 1;
	/* Total value of the sequence. */
	struct move_stats seq_value = { .playouts = 0 };
	/* The last "significant" node along the descent (i.e. node
	 * with higher than configured number of playouts). For black
	 * and white. */
	struct tree_node *significant[2] = { NULL, NULL };
	if (n->u.playouts >= u->significant_threshold)
		significant[node_color - 1] = n;

	int result;
	int pass_limit = (board_size(&b2) - 2) * (board_size(&b2) - 2) / 2;
	int passes = is_pass(b->last_move.coord) && b->moves > 0;

	/* debug */
	static char spaces[] = "\0                                                      ";
	/* /debug */
	if (UDEBUGL(8))
		fprintf(stderr, "--- UCT walk with color %d\n", player_color);

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
			u->policy->descend(u->policy, t, &descent[dlen], parity, b2.moves > pass_limit);
		else
			u->random_policy->descend(u->random_policy, t, &descent[dlen], parity, b2.moves > pass_limit);


		/*** Perform the descent: */

		if (descent[dlen].node->u.playouts >= u->significant_threshold) {
			significant[node_color - 1] = descent[dlen].node;
		}

		seq_value.playouts += descent[dlen].value.playouts;
		seq_value.value += descent[dlen].value.value * descent[dlen].value.playouts;
		n = descent[dlen++].node;
		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "%s+-- UCT sent us to [%s:%d] %d,%f\n",
			        spaces, coord2sstr(n->coord, t->board),
				n->coord, n->u.playouts,
				tree_node_get_value(t, parity, n->u.value));

		/* Add virtual loss if we need to; this is used to discourage
		 * other threads from visiting this node in case of multiple
		 * threads doing the tree search. */
		if (u->virtual_loss)
			stats_add_result(&n->u, node_color == S_BLACK ? 0.0 : 1.0, u->virtual_loss);

		assert(n->coord >= -1);
		if (amaf && !is_pass(n->coord))
			record_amaf_move(amaf, n->coord, node_color);

		struct move m = { n->coord, node_color };
		int res = board_play(&b2, &m);

		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */
		    || b2.superko_violation) {
			if (UDEBUGL(4)) {
				for (struct tree_node *ni = n; ni; ni = ni->parent)
					fprintf(stderr, "%s<%"PRIhash"> ", coord2sstr(ni->coord, t->board), ni->hash);
				fprintf(stderr, "marking invalid %s node %d,%d res %d group %d spk %d\n",
				        stone2str(node_color), coord_x(n->coord,b), coord_y(n->coord,b),
					res, group_at(&b2, m.coord), b2.superko_violation);
			}
			n->hints |= TREE_HINT_INVALID;
			result = 0;
			goto end;
		}

		if (is_pass(n->coord))
			passes++;
		else
			passes = 0;
	}

	if (amaf) {
		amaf->game_baselen = amaf->gamelen;
		amaf->record_nakade = u->playout_amaf_nakade;
	}

	if (t->use_extra_komi && u->dynkomi->persim) {
		b2.komi += round(u->dynkomi->persim(u->dynkomi, &b2, t, n));
	}

	if (passes >= 2) {
		/* XXX: No dead groups support. */
		floating_t score = board_official_score(&b2, NULL);
		/* Result from black's perspective (no matter who
		 * the player; black's perspective is always
		 * what the tree stores. */
		result = - (score * 2);

		if (UDEBUGL(5))
			fprintf(stderr, "[%d..%d] %s p-p scoring playout result %d (W %f)\n",
				player_color, node_color, coord2sstr(n->coord, t->board), result, score);
		if (UDEBUGL(6))
			board_print(&b2, stderr);

		board_ownermap_fill(&u->ownermap, &b2);

	} else { // assert(tree_leaf_node(n));
		/* In case of parallel tree search, the assertion might
		 * not hold if two threads chew on the same node. */
		result = uct_leaf_node(u, &b2, player_color, amaf, descent, &dlen, significant, t, n, node_color, spaces);
	}

	if (amaf && u->playout_amaf_cutoff) {
		unsigned int cutoff = amaf->game_baselen;
		cutoff += (amaf->gamelen - amaf->game_baselen) * u->playout_amaf_cutoff / 100;
		/* Now, reconstruct the amaf map. */
		memset(amaf->map, 0, board_size2(&b2) * sizeof(*amaf->map));
		for (unsigned int i = 0; i < cutoff; i++) {
			coord_t coord = amaf->game[i].coord;
			enum stone color = amaf->game[i].color;
			if (amaf->map[coord] == S_NONE || amaf->map[coord] == color) {
				amaf->map[coord] = color;
			/* Nakade always recorded for in-tree part */
			} else if (amaf->record_nakade || i <= amaf->game_baselen) {
				amaf_op(amaf->map[n->coord], +);
			}
		}
	}

	/* Record the result. */

	assert(n == t->root || n->parent);
	floating_t rval = scale_value(u, b, result);
	u->policy->update(u->policy, t, n, node_color, player_color, amaf, &b2, rval);

	if (t->use_extra_komi) {
		stats_add_result(&u->dynkomi->score, result / 2, 1);
		stats_add_result(&u->dynkomi->value, rval, 1);
	}

	if (u->local_tree && n->parent && !is_pass(n->coord) && dlen > 0) {
		/* Possibly transform the rval appropriately. */
		if (!u->local_tree_rootseqval) {
			floating_t expval = seq_value.value / seq_value.playouts;
			rval = stats_temper_value(rval, expval, u->local_tree);
		}

		/* Get the local sequences and record them in ltree. */
		/* We will look for sequence starts in our descent
		 * history, then run record_local_sequence() for each
		 * found sequence start; record_local_sequence() may
		 * pick longer sequences from descent history then,
		 * which is expected as it will create new lnodes. */
		enum stone seq_color = player_color;
		/* First move always starts a sequence. */
		record_local_sequence(u, t, descent, dlen, 1, seq_color, rval);
		seq_color = stone_other(seq_color);
		for (int dseqi = 2; dseqi < dlen; dseqi++, seq_color = stone_other(seq_color)) {
			if (u->local_tree_allseq) {
				/* We are configured to record all subsequences. */
				record_local_sequence(u, t, descent, dlen, dseqi, seq_color, rval);
				continue;
			}
			if (descent[dseqi].node->d >= u->tenuki_d) {
				/* Tenuki! Record the fresh sequence. */
				record_local_sequence(u, t, descent, dlen, dseqi, seq_color, rval);
				continue;
			}
			if (descent[dseqi].lnode && !descent[dseqi].lnode) {
				/* Record result for in-descent picked sequence. */
				record_local_sequence(u, t, descent, dlen, dseqi, seq_color, rval);
				continue;
			}
		}
	}

end:
	/* We need to undo the virtual loss we added during descend. */
	if (u->virtual_loss) {
		floating_t loss = node_color == S_BLACK ? 0.0 : 1.0;
		for (; n->parent; n = n->parent) {
			stats_rm_result(&n->u, loss, u->virtual_loss);
			loss = 1.0 - loss;
		}
	}

	if (amaf) {
		free(amaf->map - 1);
		free(amaf);
	}
	board_done_noalloc(&b2);
	return result;
}

int
uct_playouts(struct uct *u, struct board *b, enum stone color, struct tree *t, struct time_info *ti)
{
	int i;
	if (ti && ti->dim == TD_GAMES) {
		for (i = 0; t->root->u.playouts <= ti->len.games; i++)
			uct_playout(u, b, color, t);
	} else {
		for (i = 0; !uct_halt; i++)
			uct_playout(u, b, color, t);
	}
	return i;
}
