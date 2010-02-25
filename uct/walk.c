#include <assert.h>
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
#include "playout/elo.h"
#include "probdist.h"
#include "random.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"

float
uct_linear_dynkomi(struct uct *u, struct board *b)
{
	float extra_komi = board_effective_handicap(b, u->handicap_value) * (u->dynkomi_moves - b->moves) / u->dynkomi_moves;
	return extra_komi;
}

void
uct_progress_status(struct uct *u, struct tree *t, enum stone color, int playouts)
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

	/* Max depth */
	fprintf(stderr, "deepest % 2d ", t->max_depth - t->root->depth);

	/* Best sequence */
	fprintf(stderr, "| seq ");
	for (int depth = 0; depth < 6; depth++) {
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


struct uct_playout_callback {
	struct uct *uct;
	struct tree *tree;
	struct tree_node *lnode;
};

static void
uct_playout_probdist(void *data, struct board *b, enum stone to_play, struct probdist *pd)
{
	/* Create probability distribution according to found local tree
	 * sequence. */
	struct uct_playout_callback *upc = data;
	assert(upc && upc->tree && pd && b);
	coord_t c = b->last_move.coord;
	enum stone color = b->last_move.color;

	if (is_pass(c)) {
		/* Break local sequence. */
		upc->lnode = NULL;
	} else if (upc->lnode) {
		/* Try to follow local sequence. */
		upc->lnode = tree_get_node(upc->tree, upc->lnode, c, false);
	}

	if (!upc->lnode || !upc->lnode->children) {
		/* There's no local sequence, start new one! */
		upc->lnode = color == S_BLACK ? upc->tree->ltree_black : upc->tree->ltree_white;
		upc->lnode = tree_get_node(upc->tree, upc->lnode, c, false);
	}

	if (!upc->lnode || !upc->lnode->children) {
		/* We have no local sequence and we cannot find any starting
		 * by node corresponding to last move. */
		if (!upc->uct->local_tree_pseqroot) {
			/* Give up then, we have nothing to contribute. */
			return;
		}
		/* Construct probability distribution from possible first
		 * sequence move. Remember that @color is color of the
		 * *last* move. */
		upc->lnode = color == S_BLACK ? upc->tree->ltree_white : upc->tree->ltree_black;
		if (!upc->lnode->children) {
			/* We don't even have anything in our tree yet. */
			return;
		}
	}

	/* The probdist has the right structure only if BOARD_GAMMA is defined. */
#ifndef BOARD_GAMMA
	assert(0);
#endif

	/* Construct probability distribution from lnode children. */
	/* XXX: How to derive the appropriate gamma? */
	#define li_value(color, li) (li->u.playouts * (color == S_BLACK ? li->u.value : (1 - li->u.value)))
	#define li_gamma(color, li) (0.5 + li_value(color, li))
	struct tree_node *li = upc->lnode->children;
	assert(li);
	if (is_pass(li->coord)) {
		/* Tenuki. */
		/* TODO: Spread tenuki gamma over all moves we don't touch. */
		li = li->sibling;
	}
	for (; li; li = li->sibling) {
		if (board_at(b, li->coord) != S_NONE)
			continue;
		probdist_set(pd, li->coord, pd->items[li->coord] * li_gamma(to_play, li));
	}
}


static int
uct_leaf_node(struct uct *u, struct board *b, enum stone player_color,
              struct playout_amafmap *amaf,
              struct tree *t, struct tree_node *n, enum stone node_color,
	      char *spaces)
{
	enum stone next_color = stone_other(node_color);
	int parity = (next_color == player_color ? 1 : -1);

	/* If we don't anticipate well the opponent move during pondering
	 * (the played move has few playouts) we still need more memory
         * during genmove to explore the tree actually played.
	 * For fast_alloc, the tree compaction will free enough memory
	 * immediately. */
	unsigned long max_tree_size = u->max_tree_size;
	if (u->pondering && !u->fast_alloc)
		max_tree_size = (max_tree_size * (100 - MIN_FREE_MEM_PERCENT)) / 100;

	/* We need to make sure only one thread expands the node. If
	 * we are unlucky enough for two threads to meet in the same
	 * node, the latter one will simply do another simulation from
	 * the node itself, no big deal. t->nodes_size may exceed
	 * the maximum in multi-threaded case but not by much so it's ok.
	 * The size test must be before the test&set not after, to allow
	 * expansion of the node later if enough nodes have been freed. */
	if (n->u.playouts >= u->expand_p && t->nodes_size < max_tree_size
	    && !__sync_lock_test_and_set(&n->is_expanded, 1)) {
		tree_expand_node(t, n, b, next_color, u, parity);
        }
	if (UDEBUGL(7))
		fprintf(stderr, "%s*-- UCT playout #%d start [%s] %f\n",
			spaces, n->u.playouts, coord2sstr(n->coord, t->board),
			tree_node_get_value(t, parity, n->u.value));

	/* TODO: Don't necessarily restart the sequence walk when entering
	 * playout. */
	struct uct_playout_callback upc = { .uct = u, .tree = t, .lnode = NULL };
	if (u->local_tree_playout) {
		/* N.B.: We know this is ELO playout. */
		playout_elo_callback(u->playout, uct_playout_probdist, &upc);
	}

	struct playout_setup ps = { .gamelen = u->gamelen, .mercymin = u->mercymin };
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

static float
scale_value(struct uct *u, struct board *b, int result)
{
	float rval = result > 0;
	if (u->val_scale) {
		int vp = u->val_points;
		if (!vp) {
			vp = board_size(b) - 1; vp *= vp; vp *= 2;
		}

		float sval = (float) abs(result) / vp;
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
		      enum stone seq_color, float rval)
{
	/* Ignore pass sequences. */
	if (is_pass(descent[di].node->coord))
		return;

#define LTREE_DEBUG if (UDEBUGL(6))
	LTREE_DEBUG fprintf(stderr, "recording result %f in local %s sequence: ",
		rval, stone2str(seq_color));
	int di0 = di;

	/* Pick the right local tree root... */
	struct tree_node *lnode = seq_color == S_BLACK ? t->ltree_black : t->ltree_white;
	lnode->u.playouts++;

	/* ...and record the sequence. */
	while (di < dlen && (di == di0 || descent[di].node->d < u->tenuki_d)) {
		LTREE_DEBUG fprintf(stderr, "%s[%d] ",
			coord2sstr(descent[di].node->coord, t->board),
			descent[di].node->d);
		lnode = tree_get_node(t, lnode, descent[di++].node->coord, true);
		assert(lnode);
		stats_add_result(&lnode->u, rval, 1);
	}

	/* Add lnode for tenuki (pass) if we descended further. */
	if (di < dlen) {
		LTREE_DEBUG fprintf(stderr, "pass ");
		lnode = tree_get_node(t, lnode, pass, true);
		assert(lnode);
		stats_add_result(&lnode->u, rval, 1);
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
		amaf = calloc(1, sizeof(*amaf));
		amaf->map = calloc(board_size2(&b2) + 1, sizeof(*amaf->map));
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
	#define DLEN 512
	struct uct_descent descent[DLEN];
	descent[0].node = n; descent[0].lnode = NULL;
	int dlen = 1;
	/* Total value of the sequence. */
	struct move_stats seq_value = { .playouts = 0 };

	int result;
	int pass_limit = (board_size(&b2) - 2) * (board_size(&b2) - 2) / 2;
	int passes = is_pass(b->last_move.coord) && b->moves > 0;

	/* debug */
	int depth = 0;
	static char spaces[] = "\0                                                      ";
	/* /debug */
	if (UDEBUGL(8))
		fprintf(stderr, "--- UCT walk with color %d\n", player_color);

	while (!tree_leaf_node(n) && passes < 2) {
		spaces[depth++] = ' '; spaces[depth] = 0;


		/*** Choose a node to descend to: */

		/* Parity is chosen already according to the child color, since
		 * it is applied to children. */
		node_color = stone_other(node_color);
		int parity = (node_color == player_color ? 1 : -1);

		assert(dlen < DLEN);
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

		seq_value.playouts += descent[dlen].value.playouts;
		seq_value.value += descent[dlen].value.value * descent[dlen].value.playouts;
		n = descent[dlen++].node;
		assert(n == t->root || n->parent);
		if (UDEBUGL(7))
			fprintf(stderr, "%s+-- UCT sent us to [%s:%d] %f\n",
			        spaces, coord2sstr(n->coord, t->board), n->coord,
				tree_node_get_value(t, parity, n->u.value));

		/* Add virtual loss if we need to; this is used to discourage
		 * other threads from visiting this node in case of multiple
		 * threads doing the tree search. */
		if (u->virtual_loss)
			stats_add_result(&n->u, tree_parity(t, parity) > 0 ? 0 : 1, 1);

		assert(n->coord >= -1);
		if (amaf && !is_pass(n->coord)) {
			if (amaf->map[n->coord] == S_NONE || amaf->map[n->coord] == node_color) {
				amaf->map[n->coord] = node_color;
			} else { // XXX: Respect amaf->record_nakade
				amaf_op(amaf->map[n->coord], +);
			}
			amaf->game[amaf->gamelen].coord = n->coord;
			amaf->game[amaf->gamelen].color = node_color;
			amaf->gamelen++;
			assert(amaf->gamelen < sizeof(amaf->game) / sizeof(amaf->game[0]));
		}

		struct move m = { n->coord, node_color };
		int res = board_play(&b2, &m);

		if (res < 0 || (!is_pass(m.coord) && !group_at(&b2, m.coord)) /* suicide */
		    || b2.superko_violation) {
			if (UDEBUGL(3)) {
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

	if (t->use_extra_komi) {
		assert(u->dynkomi == DYNKOMI_LINEAR);
		if (b2.moves < u->dynkomi_moves)
			b2.komi += uct_linear_dynkomi(u, &b2);
	}

	if (passes >= 2) {
		/* XXX: No dead groups support. */
		float score = board_official_score(&b2, NULL);
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

	} else { assert(u->parallel_tree || tree_leaf_node(n));
		/* In case of parallel tree search, the assertion might
		 * not hold if two threads chew on the same node. */
		result = uct_leaf_node(u, &b2, player_color, amaf, t, n, node_color, spaces);
	}

	if (amaf && u->playout_amaf_cutoff) {
		int cutoff = amaf->game_baselen;
		cutoff += (amaf->gamelen - amaf->game_baselen) * u->playout_amaf_cutoff / 100;
		/* Now, reconstruct the amaf map. */
		memset(amaf->map, 0, board_size2(&b2) * sizeof(*amaf->map));
		for (int i = 0; i < cutoff; i++) {
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

	assert(n == t->root || n->parent);
	if (result != 0) {
		float rval = scale_value(u, b, result);

		u->policy->update(u->policy, t, n, node_color, player_color, amaf, rval);

		if (u->local_tree && n->parent && !is_pass(n->coord) && dlen > 0) {
			/* Possibly transform the rval appropriately. */
			float expval = seq_value.value / seq_value.playouts;
			rval = stats_temper_value(rval, expval, u->local_tree);

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
	}

end:
	/* We need to undo the virtual loss we added during descend. */
	if (u->virtual_loss) {
		int parity = (node_color == player_color ? 1 : -1);
		for (; n->parent; n = n->parent) {
			stats_rm_result(&n->u, tree_parity(t, parity) > 0 ? 0 : 1, 1);
			parity = -parity;
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
uct_playouts(struct uct *u, struct board *b, enum stone color, struct tree *t)
{
	int i;
	for (i = 0; !uct_halt; i++)
		uct_playout(u, b, color, t);
	return i;
}
