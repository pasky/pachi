#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "tactics/util.h"
#include "uct/internal.h"
#include "uct/tree.h"
#include "uct/policy/generic.h"

/* This implements the UCB1 policy with an extra AMAF heuristics. */

struct ucb1_policy_amaf {
	/* This is what the Modification of UCT with Patterns in Monte Carlo Go
	 * paper calls 'p'. Original UCB has this on 2, but this seems to
	 * produce way too wide searches; reduce this to get deeper and
	 * narrower readouts - try 0.2. */
	floating_t explore_p;
	/* Rescale virtual loss value to square root of #threads. This mitigates
	 * the number of virtual losses added in case of a large amount of
	 * threads; it seems that with linear virtual losses, overly diverse
	 * exploration caused by this may cause a wrong mean value computed
	 * for the parent node. */
	bool vloss_sqrt;
	/* In distributed mode, encourage different slaves to work on different
	 * parts of the tree by adding virtual wins to different nodes. */
	int virtual_win;
	int root_virtual_win;
	int vwin_min_playouts;
	/* First Play Urgency - if set to less than infinity (the MoGo paper
	 * above reports 1.0 as the best), new branches are explored only
	 * if none of the existing ones has higher urgency than fpu. */
	floating_t fpu;
	unsigned int equiv_rave;
	bool sylvain_rave;
        /* Give more weight to moves played earlier. */
	int distance_rave;
	/* Give 0 or negative rave bonus to ko threats before taking the ko.
	   1=normal bonus, 0=no bonus, -1=invert rave bonus, -2=double penalty... */
	int threat_rave;
	/* Coefficient of local tree values embedded in RAVE. */
	floating_t ltree_rave;
	/* Coefficient of criticality embedded in RAVE. */
	floating_t crit_rave;
	int crit_min_playouts;
	floating_t crit_plthres_coef;
	bool crit_negative;
	bool crit_negflip;
	bool crit_amaf;
	bool crit_lvalue;
};


static inline floating_t fast_sqrt(unsigned int x)
{
	static const floating_t table[] = {
		0, 1, 1.41421356237309504880, 1.73205080756887729352,
		2.00000000000000000000, 2.23606797749978969640,
		2.44948974278317809819, 2.64575131106459059050,
		2.82842712474619009760, 3.00000000000000000000,
		3.16227766016837933199, 3.31662479035539984911,
		3.46410161513775458705, 3.60555127546398929311,
		3.74165738677394138558, 3.87298334620741688517,
		4.00000000000000000000, 4.12310562561766054982,
		4.24264068711928514640, 4.35889894354067355223,
		4.47213595499957939281, 4.58257569495584000658,
		4.69041575982342955456, 4.79583152331271954159,
		4.89897948556635619639, 5.00000000000000000000,
		5.09901951359278483002, 5.19615242270663188058,
		5.29150262212918118100, 5.38516480713450403125,
		5.47722557505166113456, 5.56776436283002192211,
		5.65685424949238019520, 5.74456264653802865985,
		5.83095189484530047087, 5.91607978309961604256,
		6.00000000000000000000, 6.08276253029821968899,
		6.16441400296897645025, 6.24499799839839820584,
		6.32455532033675866399, 6.40312423743284868648,
		6.48074069840786023096, 6.55743852430200065234,
		6.63324958071079969822, 6.70820393249936908922,
		6.78232998312526813906, 6.85565460040104412493,
		6.92820323027550917410, 7.00000000000000000000,
		7.07106781186547524400, 7.14142842854284999799,
		7.21110255092797858623, 7.28010988928051827109,
		7.34846922834953429459, 7.41619848709566294871,
		7.48331477354788277116, 7.54983443527074969723,
		7.61577310586390828566, 7.68114574786860817576,
		7.74596669241483377035, 7.81024967590665439412,
		7.87400787401181101968, 7.93725393319377177150,
	};
	if (x < sizeof(table) / sizeof(*table)) {
		return table[x];
	} else {
		return sqrt(x);
	}
}

#define URAVE_DEBUG if (0)
static inline floating_t
ucb1rave_evaluate(struct uct_policy *p, struct tree *tree, struct uct_descent *descent, int parity)
{
	struct ucb1_policy_amaf *b = p->data;
	struct tree_node *node = descent->node;
	struct tree_node *lnode = descent->lnode;

	struct move_stats n = node->u, r = node->amaf;
	if (p->uct->amaf_prior) {
		stats_merge(&r, &node->prior);
	} else {
		stats_merge(&n, &node->prior);
	}

	if (p->uct->virtual_loss) {
		/* Add virtual loss if we need to; this is used to discourage
		 * other threads from visiting this node in case of multiple
		 * threads doing the tree search. */
		floating_t vloss_coeff = b->vloss_sqrt ? sqrt(p->uct->threads) / p->uct->threads : 1.;
		struct move_stats c = { .value = parity > 0 ? 0. : 1., .playouts = node->descents * vloss_coeff };
		stats_merge(&n, &c);
	}

	/* Local tree heuristics. */
	assert(!lnode || lnode->parent);
	if (p->uct->local_tree && b->ltree_rave > 0 && lnode
	    && (p->uct->local_tree_rootchoose || lnode->parent->parent)) {
		struct move_stats l = lnode->u;
		l.playouts = ((floating_t) l.playouts) * b->ltree_rave / LTREE_PLAYOUTS_MULTIPLIER;
		URAVE_DEBUG fprintf(stderr, "[ltree] adding [%s] %f%%%d to [%s] RAVE %f%%%d\n",
			coord2sstr(node_coord(lnode), tree->board), l.value, l.playouts,
			coord2sstr(node_coord(node), tree->board), r.value, r.playouts);
		stats_merge(&r, &l);
	}

	/* Criticality heuristics. */
	if (b->crit_rave > 0 && (b->crit_plthres_coef > 0
				 ? node->u.playouts > tree->root->u.playouts * b->crit_plthres_coef
				 : node->u.playouts > b->crit_min_playouts)) {
		floating_t crit = tree_node_criticality(tree, node);
		if (b->crit_negative || crit > 0) {
			floating_t val = 1.0f;
			if (b->crit_negflip && crit < 0) {
				val = 0;
				crit = -crit;
			}
			struct move_stats c = {
				.value = tree_node_get_value(tree, parity, val),
				.playouts = crit * r.playouts * b->crit_rave
			};
			URAVE_DEBUG fprintf(stderr, "[crit] adding %f%%%d to [%s] RAVE %f%%%d\n",
				c.value, c.playouts,
				coord2sstr(node_coord(node), tree->board), r.value, r.playouts);
			stats_merge(&r, &c);
		}
	}

	floating_t value = 0;
	if (n.playouts) {
		if (r.playouts) {
			/* At the beginning, beta is at 1 and RAVE is used.
			 * At b->equiv_rate, beta is at 1/3 and gets steeper on. */
			floating_t beta;
			if (b->sylvain_rave) {
				beta = (floating_t) r.playouts / (r.playouts + n.playouts
					+ (floating_t) n.playouts * r.playouts / b->equiv_rave);
			} else {
				/* XXX: This can be cached in descend; but we don't use this by default. */
				beta = sqrt(b->equiv_rave / (3 * node->parent->u.playouts + b->equiv_rave));
			}

			value = beta * r.value + (1.f - beta) * n.value;
			URAVE_DEBUG fprintf(stderr, "\t%s value = %f * %f + (1 - %f) * %f (prior %f)\n",
			        coord2sstr(node_coord(node), tree->board), beta, r.value, beta, n.value, node->prior.value);
		} else {
			value = n.value;
			URAVE_DEBUG fprintf(stderr, "\t%s value = %f (prior %f)\n",
			        coord2sstr(node_coord(node), tree->board), n.value, node->prior.value);
		}
	} else if (r.playouts) {
		value = r.value;
		URAVE_DEBUG fprintf(stderr, "\t%s value = rave %f (prior %f)\n",
			coord2sstr(node_coord(node), tree->board), r.value, node->prior.value);
	}
	descent->value.playouts = r.playouts + n.playouts;
	descent->value.value = value;

	return tree_node_get_value(tree, parity, value);
}

void
ucb1rave_descend(struct uct_policy *p, struct tree *tree, struct uct_descent *descent, int parity, bool allow_pass)
{
	struct ucb1_policy_amaf *b = p->data;
	floating_t nconf = 1.f;
	if (b->explore_p > 0)
		nconf = sqrt(log(descent->node->u.playouts + descent->node->prior.playouts));
	struct uct *u = p->uct;
	int vwin = 0;
	if (u->max_slaves > 0 && u->slave_index >= 0)
		vwin = descent->node == tree->root ? b->root_virtual_win : b->virtual_win;
	int child = 0;

	uctd_try_node_children(tree, descent, allow_pass, parity, u->tenuki_d, di, urgency) {
		struct tree_node *ni = di.node;
		urgency = ucb1rave_evaluate(p, tree, &di, parity);

		/* In distributed mode, encourage different slaves to work on different
		 * parts of the tree. We rely on the fact that children (if they exist)
		 * are the same and in the same order in all slaves. */
		if (vwin > 0 && ni->u.playouts > b->vwin_min_playouts && (child - u->slave_index) % u->max_slaves == 0)
			urgency += vwin / (ni->u.playouts + vwin);

		if (ni->u.playouts > 0 && b->explore_p > 0) {
			urgency += b->explore_p * nconf / fast_sqrt(ni->u.playouts);

		} else if (ni->u.playouts + ni->amaf.playouts + ni->prior.playouts == 0) {
			/* assert(!u->even_eqex); */
			urgency = b->fpu;
		}
	} uctd_set_best_child(di, urgency);

	uctd_get_best_child(descent);
}


/* Return the length of the current ko (number of moves up to to the last ko capture),
 * 0 if the sequence is empty or doesn't start with a ko capture.
 *   B captures a ko
 *   W plays a ko threat
 *   B answers ko threat
 *   W re-captures the ko  <- return 4
 *   B plays a ko threat
 *   W connects the ko */
static inline int ko_length(bool *ko_capture_map, int map_length)
{
	if (map_length <= 0 || !ko_capture_map[0]) return 0;
	int length = 1;
	while (length + 2 < map_length && ko_capture_map[length + 2]) length += 3;
	return length;
}

void
ucb1amaf_update(struct uct_policy *p, struct tree *tree, struct tree_node *node,
		enum stone node_color, enum stone player_color,
		struct playout_amafmap *map, struct board *final_board,
		floating_t result)
{
	struct ucb1_policy_amaf *b = p->data;
	enum stone winner_color = result > 0.5 ? S_BLACK : S_WHITE;

	/* Record of the random playout - for each intersection coord,
	 * first_move[coord] is the index map->game of the first move
	 * at this coordinate, or INT_MAX if the move was not played.
	 * The parity gives the color of this move.
	 */
	int first_map[board_size2(final_board)+1];
	int *first_move = &first_map[1]; // +1 for pass

#if 0
	struct board bb; bb.size = 9+2;
	for (struct tree_node *ni = node; ni; ni = ni->parent)
		fprintf(stderr, "%s ", coord2sstr(node_coord(ni), &bb));
	fprintf(stderr, "[color %d] update result %d (color %d)\n",
			node_color, result, player_color);
#endif

	/* Initialize first_move */
	for (int i = pass; i < board_size2(final_board); i++) first_move[i] = INT_MAX;
	int move;
	assert(map->gamelen > 0);
	for (move = map->gamelen - 1; move >= map->game_baselen; move--)
		first_move[map->game[move]] = move;

	while (node) {
		if (!b->crit_amaf && !is_pass(node_coord(node))) {
			stats_add_result(&node->winner_owner, board_local_value(b->crit_lvalue, final_board, node_coord(node), winner_color), 1);
			stats_add_result(&node->black_owner, board_local_value(b->crit_lvalue, final_board, node_coord(node), S_BLACK), 1);
		}
		stats_add_result(&node->u, result, 1);

		bool *ko_capture_map = &map->is_ko_capture[move+1];
		int max_threat_dist = b->threat_rave <= 0 ? ko_length(ko_capture_map, map->gamelen - (move+1)) : -1;

		/* This loop ignores symmetry considerations, but they should
		 * matter only at a point when AMAF doesn't help much. */
		assert(map->game_baselen >= 0);
		for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
			if (is_pass(node_coord(ni))) continue;

			/* Use the child move only if it was first played by the same color. */
			int first = first_move[node_coord(ni)];
			if (first == INT_MAX) continue;
			assert(first > move && first < map->gamelen);
			int distance = first - (move + 1);
			if (distance & 1) continue;

			int weight = 1;
			floating_t res = result;

			/* Don't give amaf bonus to a ko threat before taking the ko.
			 * http://www.grappa.univ-lille3.fr/~coulom/Aja_PhD_Thesis.pdf
			 */
			if (distance <= max_threat_dist && distance % 6 == 4) {
				weight = - b->threat_rave;
				res = 1.0 - res;
			} else if (b->distance_rave != 0) {
				/* Give more weight to moves played earlier */
				weight += b->distance_rave * (map->gamelen - first) / (map->gamelen - move);
			}
			stats_add_result(&ni->amaf, res, weight);

			if (b->crit_amaf) {
				stats_add_result(&ni->winner_owner, board_local_value(b->crit_lvalue, final_board, node_coord(ni), winner_color), 1);
				stats_add_result(&ni->black_owner, board_local_value(b->crit_lvalue, final_board, node_coord(ni), S_BLACK), 1);
			}
#if 0
			struct board bb; bb.size = 9+2;
			fprintf(stderr, "* %s<%"PRIhash"> -> %s<%"PRIhash"> [%d/%f => %d/%f]\n",
				coord2sstr(node_coord(node), &bb), node->hash,
				coord2sstr(node_coord(ni), &bb), ni->hash,
				player_color, result, move, res);
#endif
		}
		if (node->parent) {
			assert(move >= 0 && map->game[move] == node_coord(node) && first_move[node_coord(node)] > move);
			first_move[node_coord(node)] = move;
			move--;
		}
		node = node->parent;
	}
}


struct uct_policy *
policy_ucb1amaf_init(struct uct *u, char *arg, struct board *board)
{
	struct uct_policy *p = calloc2(1, sizeof(*p));
	struct ucb1_policy_amaf *b = calloc2(1, sizeof(*b));
	p->uct = u;
	p->data = b;
	p->choose = uctp_generic_choose;
	p->winner = uctp_generic_winner;
	p->evaluate = ucb1rave_evaluate;
	p->descend = ucb1rave_descend;
	p->update = ucb1amaf_update;
	p->wants_amaf = true;

	b->explore_p = 0;
	b->equiv_rave = board_large(board) ? 4000 : 3000;
	b->fpu = INFINITY;
	b->sylvain_rave = true;
	b->distance_rave = 3;
	b->threat_rave = 0;
	b->ltree_rave = 0.75f;

	b->crit_rave = 1.1f;
	b->crit_min_playouts = 2000;
	b->crit_negative = 1;
	b->crit_amaf = 0;

	b->vloss_sqrt = true;

	b->virtual_win = 5;
	b->root_virtual_win = 30;
	b->vwin_min_playouts = 1000;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "explore_p")) {
				b->explore_p = atof(optval);
			} else if (!strcasecmp(optname, "fpu") && optval) {
				b->fpu = atof(optval);
			} else if (!strcasecmp(optname, "equiv_rave") && optval) {
				b->equiv_rave = atof(optval);
			} else if (!strcasecmp(optname, "sylvain_rave")) {
				b->sylvain_rave = !optval || *optval == '1';
			} else if (!strcasecmp(optname, "distance_rave") && optval) {
				b->distance_rave = atoi(optval);
			} else if (!strcasecmp(optname, "threat_rave") && optval) {
				b->threat_rave = atoi(optval);
			} else if (!strcasecmp(optname, "ltree_rave") && optval) {
				b->ltree_rave = atof(optval);
			} else if (!strcasecmp(optname, "crit_rave") && optval) {
				b->crit_rave = atof(optval);
			} else if (!strcasecmp(optname, "crit_min_playouts") && optval) {
				b->crit_min_playouts = atoi(optval);
			} else if (!strcasecmp(optname, "crit_plthres_coef") && optval) {
				b->crit_plthres_coef = atof(optval);
			} else if (!strcasecmp(optname, "crit_negative")) {
				b->crit_negative = !optval || *optval == '1';
			} else if (!strcasecmp(optname, "crit_negflip")) {
				b->crit_negflip = !optval || *optval == '1';
			} else if (!strcasecmp(optname, "crit_amaf")) {
				b->crit_amaf = !optval || *optval == '1';
			} else if (!strcasecmp(optname, "crit_lvalue")) {
				b->crit_lvalue = !optval || *optval == '1';
			} else if (!strcasecmp(optname, "virtual_win") && optval) {
				b->virtual_win = atoi(optval);
			} else if (!strcasecmp(optname, "root_virtual_win") && optval) {
				b->root_virtual_win = atoi(optval);
			} else if (!strcasecmp(optname, "vwin_min_playouts") && optval) {
				b->vwin_min_playouts = atoi(optval);
			} else if (!strcasecmp(optname, "vloss_sqrt")) {
				b->vloss_sqrt = !optval || *optval == '1';
			} else {
				fprintf(stderr, "ucb1amaf: Invalid policy argument %s or missing value\n",
					optname);
				exit(1);
			}
		}
	}

	return p;
}
