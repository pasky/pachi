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
#include "probdist.h"
#include "random.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/uct.h"
#include "uct/walk.h"


float
uct_get_extra_komi(struct uct *u, struct board *b)
{
	float extra_komi = board_effective_handicap(b) * (u->dynkomi - b->moves) / u->dynkomi;
	return extra_komi;
}

void
uct_progress_status(struct uct *u, struct tree *t, enum stone color, int playouts)
{
	if (!UDEBUGL(0))
		return;

	/* Best move */
	struct tree_node *best = u->policy->choose(u->policy, t->root, t->board, color);
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
			best = u->policy->choose(u->policy, best, t->board, color);
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


static int
uct_leaf_node(struct uct *u, struct board *b, enum stone player_color,
              struct playout_amafmap *amaf,
              struct tree *t, struct tree_node *n, enum stone node_color,
	      char *spaces)
{
	enum stone next_color = stone_other(node_color);
	int parity = (next_color == player_color ? 1 : -1);
	if (n->u.playouts >= u->expand_p) {
		// fprintf(stderr, "expanding %s (%p ^-%p)\n", coord2sstr(n->coord, b), n, n->parent);
		if (!u->parallel_tree) {
			/* Single-threaded, life is easy. */
			tree_expand_node(t, n, b, next_color, u, parity);
		} else {
			/* We need to make sure only one thread expands
			 * the node. If we are unlucky enough for two
			 * threads to meet in the same node, the latter
			 * one will simply do another simulation from
			 * the node itself, no big deal. */
			pthread_mutex_lock(&t->expansion_mutex);
			if (tree_leaf_node(n)) {
				tree_expand_node(t, n, b, next_color, u, parity);
			} else {
				// fprintf(stderr, "cancelling expansion, thread collision\n");
			}
			pthread_mutex_unlock(&t->expansion_mutex);
		}
	}
	if (UDEBUGL(7))
		fprintf(stderr, "%s*-- UCT playout #%d start [%s] %f\n",
			spaces, n->u.playouts, coord2sstr(n->coord, t->board),
			tree_node_get_value(t, parity, n->u.value));

	struct uct_board *ub = b->es; assert(ub);
	int result = play_random_game(b, next_color, u->gamelen,
	                              u->playout_amaf ? amaf : NULL,
				      &ub->ownermap, u->playout);
	if (next_color == S_WHITE) {
		/* We need the result from black's perspective. */
		result = - result;
	}
	if (UDEBUGL(7))
		fprintf(stderr, "%s -- [%d..%d] %s random playout result %d\n",
		        spaces, player_color, next_color, coord2sstr(n->coord, t->board), result);

	return result;
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

		/* Parity is chosen already according to the child color, since
		 * it is applied to children. */
		node_color = stone_other(node_color);
		int parity = (node_color == player_color ? 1 : -1);
		n = (!u->random_policy_chance || fast_random(u->random_policy_chance))
			? u->policy->descend(u->policy, t, n, parity, pass_limit)
			: u->random_policy->descend(u->random_policy, t, n, parity, pass_limit);

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
					fprintf(stderr, "%s<%lld> ", coord2sstr(ni->coord, t->board), ni->hash);
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

	if (u->dynkomi > b2.moves && (player_color & u->dynkomi_mask))
		b2.komi += uct_get_extra_komi(u, &b2);

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

		struct uct_board *ub = b->es; assert(ub);
		playout_ownermap_fill(&ub->ownermap, &b2);

	} else { assert(tree_leaf_node(n));
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
		u->policy->update(u->policy, t, n, node_color, player_color, amaf, rval);

		if (u->root_heuristic && n->parent) {
			if (!t->chvals) {
				t->chvals = calloc(board_size2(b), sizeof(t->chvals[0]));
				t->chchvals = calloc(board_size2(b), sizeof(t->chchvals[0]));
			}

			/* Possibly transform the rval appropriately. */
			rval = stats_temper_value(rval, n->parent->u.value, u->root_heuristic);

			struct tree_node *ni = n;
			while (ni->parent->parent && ni->parent->parent->parent)
				ni = ni->parent;
			if (ni->parent->parent) {
				if (likely(!is_pass(ni->coord)))
					stats_add_result(&t->chchvals[ni->coord], rval, 1);
				ni = ni->parent;
			}
			assert(ni->parent && !ni->parent->parent);

			if (likely(!is_pass(ni->coord)))
				stats_add_result(&t->chvals[ni->coord], rval, 1);
		}
	}

end:
	/* We need to undo the virtual loss we added during descend. */
	if (u->virtual_loss) {
		int parity = (node_color == player_color ? 1 : -1);
		for (; n->parent; n = n->parent) {
			stats_rm_result(&n->u, tree_parity(t, parity) > 0 ? 0 : 1, 1);
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
	/* Should we print progress info? In case all threads work on the same
	 * tree, only the first thread does. */
	#define ok_to_talk (!u->parallel_tree || !thread_id)

	int i, games = u->games;
	if (t->root->children)
		games -= t->root->u.playouts / 1.5;
	/* else this is highly read-out but dead-end branch of opening book;
	 * we need to start from scratch; XXX: Maybe actually base the readout
	 * count based on number of playouts of best node? */
	if (games < u->games && UDEBUGL(2))
		fprintf(stderr, "<pre-simulated %d games skipped>\n", u->games - games);
	for (i = 0; i < games; i++) {
		int result = uct_playout(u, b, color, t);
		if (result == 0) {
			/* Tree descent has hit invalid move. */
			continue;
		}

		if (unlikely(ok_to_talk && i > 0 && !(i % 10000))) {
			uct_progress_status(u, t, color, i);
		}

		if (i > 0 && !(i % 500)) {
			struct tree_node *best = u->policy->choose(u->policy, t->root, b, color);
			if (best && ((best->u.playouts >= 2000 && tree_node_get_value(t, 1, best->u.value) >= u->loss_threshold)
			             || (best->u.playouts >= 500 && tree_node_get_value(t, 1, best->u.value) >= 0.95)))
				break;
		}

		if (uct_halt) {
			if (UDEBUGL(2))
				fprintf(stderr, "<halting early, %d games skipped>\n", games - i);
			break;
		}
	}

	if (ok_to_talk) {
		uct_progress_status(u, t, color, i);
		if (UDEBUGL(3))
			tree_dump(t, u->dumpthres);
	}
	return i;
}
