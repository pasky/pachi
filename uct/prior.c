#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "tactics.h"
#include "uct/internal.h"
#include "uct/prior.h"
#include "uct/tree.h"

/* Applying heuristic values to the tree nodes, skewing the reading in
 * most interesting directions. */


struct uct_prior {
	/* Equivalent experience for prior knowledge. MoGo paper recommends
	 * 50 playouts per source; in practice, esp. with RAVE, about 6
	 * playouts per source seems best. */
	int eqex;
	int even_eqex, gp_eqex, policy_eqex, b19_eqex, cfgd_eqex, eye_eqex, ko_eqex;
};

void
uct_prior_even(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{even} */
	/* This may be dubious for normal UCB1 but is essential for
	 * reading stability of RAVE, it appears. */
	foreach_point_and_pass(map->b) {
		if (!map->consider[c])
			continue;
		add_prior_value(map, c, 0.5, u->prior->even_eqex);
	} foreach_point_end;
}

void
uct_prior_eye(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Discourage playing into our own eyes. However, we cannot
	 * completely prohibit it:
	 * #######
	 * ...XX.#
	 * XOOOXX#
	 * X.OOOO#
	 * .XXXX.# */
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		if (!board_is_one_point_eye(map->b, &c, map->to_play))
			continue;
		add_prior_value(map, c, 0, u->prior->eye_eqex);
	} foreach_point_end;
}

void
uct_prior_ko(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Favor fighting ko, if we took it le 10 moves ago. */
	coord_t ko = map->b->last_ko.coord;
	if (is_pass(ko) || map->b->moves - map->b->last_ko_age > 10 || !map->consider[ko])
		return;
	// fprintf(stderr, "prior ko-fight @ %s %s\n", stone2str(map->to_play), coord2sstr(ko, map->b));
	add_prior_value(map, ko, 1, u->prior->ko_eqex);
}

void
uct_prior_b19(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{b19} */
	/* Specific hints for 19x19 board - priors for certain edge distances. */
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		int d = coord_edge_distance(c, map->b);
		if (d != 1 && d != 3)
			continue;
		/* The bonus applies only with no stones in immediate
		 * vincinity. */
		if (board_stone_radar(map->b, c, 2))
			continue;
		/* First line: 0 */
		/* Third line: 1 */
		add_prior_value(map, c, d == 3, u->prior->b19_eqex);
	} foreach_point_end;
}

void
uct_prior_grandparent(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{grandparent} */
	foreach_point_and_pass(map->b) {
		if (!map->consider[c])
			continue;
		if (!node->parent || !node->parent->parent)
			continue;
		struct tree_node *gpp = node->parent->parent;
		for (struct tree_node *ni = gpp->children; ni; ni = ni->sibling) {
			/* Be careful not to emphasize too random results. */
			if (ni->coord == node->coord && ni->u.playouts > u->prior->gp_eqex) {
				/* We purposefuly ignore the parity. */
				stats_add_result(&map->prior[c], ni->u.value, u->prior->gp_eqex);
			}
		}
	} foreach_point_end;
}

void
uct_prior_playout(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{playout-policy} */
	if (u->playout->assess)
		u->playout->assess(u->playout, map, u->prior->policy_eqex);
}

void
uct_prior_cfgd(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{common_fate_graph_distance} */
	/* Give bonus to moves local to the last move, where "local" means
	 * local in terms of groups, not just manhattan distance. */
	if (is_pass(map->b->last_move.coord))
		return;

	int distances[board_size2(map->b)];
	cfg_distances(map->b, map->b->last_move.coord, distances, 3);
	foreach_point(map->b) {
		if (!map->consider[c])
			continue;
		// fprintf(stderr, "distance %s-%s: %d\n", coord2sstr(map->b->last_move.coord, map->b), coord2sstr(c, map->b), distances[c]);
		if (distances[c] > 3)
			continue;
		assert(distances[c] != 0);
		int bonuses[] = { 0, u->prior->cfgd_eqex, u->prior->cfgd_eqex / 2, u->prior->cfgd_eqex / 2 };
		int bonus = bonuses[distances[c]];
		add_prior_value(map, c, 1, bonus);
	} foreach_point_end;
}

void
uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	if (u->prior->even_eqex)
		uct_prior_even(u, node, map);
	if (u->prior->eye_eqex)
		uct_prior_eye(u, node, map);
	if (u->prior->ko_eqex)
		uct_prior_ko(u, node, map);
	if (u->prior->b19_eqex)
		uct_prior_b19(u, node, map);
	if (u->prior->gp_eqex)
		uct_prior_grandparent(u, node, map);
	if (u->prior->policy_eqex)
		uct_prior_playout(u, node, map);
	if (u->prior->cfgd_eqex)
		uct_prior_cfgd(u, node, map);
}

struct uct_prior *
uct_prior_init(char *arg)
{
	struct uct_prior *p = calloc(1, sizeof(struct uct_prior));

	// gp: 14 vs 0: 44% (+-3.5)
	p->gp_eqex = p->ko_eqex = 0;
	p->even_eqex = p->policy_eqex = p->b19_eqex = p->cfgd_eqex = p->eye_eqex = -1;
	p->eqex = 40; /* Even number! */

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "eqex") && optval) {
				p->eqex = atoi(optval);

			} else if (!strcasecmp(optname, "even") && optval) {
				p->even_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "gp") && optval) {
				p->gp_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "policy") && optval) {
				p->policy_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "b19") && optval) {
				p->b19_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "cfgd") && optval) {
				p->cfgd_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "eye") && optval) {
				p->eye_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "ko") && optval) {
				p->ko_eqex = atoi(optval);
			} else {
				fprintf(stderr, "uct: Invalid prior argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	if (p->even_eqex < 0) p->even_eqex = p->eqex;
	if (p->gp_eqex < 0) p->gp_eqex = p->eqex;
	if (p->policy_eqex < 0) p->policy_eqex = p->eqex;
	if (p->b19_eqex < 0) p->b19_eqex = p->eqex;
	if (p->cfgd_eqex < 0) p->cfgd_eqex = p->eqex;
	if (p->eye_eqex < 0) p->eye_eqex = p->eqex;
	if (p->ko_eqex < 0) p->ko_eqex = p->eqex;

	return p;
}
