#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "joseki/base.h"
#include "move.h"
#include "random.h"
#include "tactics/ladder.h"
#include "tactics/util.h"
#include "uct/internal.h"
#include "uct/plugins.h"
#include "uct/prior.h"
#include "uct/tree.h"

/* Applying heuristic values to the tree nodes, skewing the reading in
 * most interesting directions. */

/* TODO: Introduce foreach_fpoint() to iterate only over non-occupied
 * positions. */

struct uct_prior {
	/* Equivalent experience for prior knowledge. MoGo paper recommends
	 * 50 playouts per source; in practice, esp. with RAVE, about 6
	 * playouts per source seems best. */
	int eqex;
	int even_eqex, policy_eqex, b19_eqex, eye_eqex, ko_eqex, plugin_eqex, joseki_eqex, pattern_eqex;
	int cfgdn; int *cfgd_eqex;
	bool prune_ladders;
};

void
uct_prior_even(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{even} */
	/* This may be dubious for normal UCB1 but is essential for
	 * reading stability of RAVE, it appears. */
	add_prior_value(map, pass, 0.5, u->prior->even_eqex);
	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;
		add_prior_value(map, c, 0.5, u->prior->even_eqex);
	} foreach_free_point_end;
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
	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;
		if (!board_is_one_point_eye(map->b, c, map->to_play))
			continue;
		add_prior_value(map, c, 0, u->prior->eye_eqex);
	} foreach_free_point_end;
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
	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;
		int d = coord_edge_distance(c, map->b);
		if (d != 0 && d != 2)
			continue;
		/* The bonus applies only with no stones in immediate
		 * vincinity. */
		if (board_stone_radar(map->b, c, 2))
			continue;
		/* First line: 0 */
		/* Third line: 1 */
		add_prior_value(map, c, d == 2, u->prior->b19_eqex);
	} foreach_free_point_end;
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
	if (is_pass(map->b->last_move.coord) || is_resign(map->b->last_move.coord))
		return;

	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;
		if (map->distances[c] > u->prior->cfgdn)
			continue;
		assert(map->distances[c] != 0);
		int bonus = u->prior->cfgd_eqex[map->distances[c]];
		add_prior_value(map, c, 1, bonus);
	} foreach_free_point_end;
}

void
uct_prior_joseki(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{joseki} */
	if (!u->jdict)
		return;
	for (int i = 0; i < 4; i++) {
		hash_t h = map->b->qhash[i] & joseki_hash_mask;
		coord_t *cc = u->jdict->patterns[h].moves[map->to_play - 1];
		if (!cc) continue;
		for (; !is_pass(*cc); cc++) {
			if (coord_quadrant(*cc, map->b) != i)
				continue;
			add_prior_value(map, *cc, 1.0, u->prior->joseki_eqex);
		}
	}
}

void
uct_prior_pattern(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	/* Q_{pattern} */
	if (!u->pat.pd)
		return;

	struct board *b = map->b;
	struct pattern pats[b->flen];
	floating_t probs[b->flen];
	pattern_rate_moves(&u->pat, b, map->to_play, pats, probs);
	if (UDEBUGL(5)) {
		fprintf(stderr, "Pattern prior at node %s\n", coord2sstr(node->coord, b));
		board_print(b, stderr);
	}

	for (int f = 0; f < b->flen; f++) {
		if (isnan(probs[f]) || probs[f] < 0.001)
			continue;
		assert(!is_pass(b->f[f]));
		if (UDEBUGL(5)) {
			char s[256]; pattern2str(s, &pats[f]);
			fprintf(stderr, "\t%s: %.3f %s\n", coord2sstr(b->f[f], b), probs[f], s);
		}
		add_prior_value(map, b->f[f], 1.0, sqrt(probs[f]) * u->prior->pattern_eqex);
	}
}

void
uct_prior(struct uct *u, struct tree_node *node, struct prior_map *map)
{
	if (u->prior->prune_ladders && !board_playing_ko_threat(map->b)) {
		foreach_free_point(map->b) {
			if (!map->consider[c])
				continue;
			group_t atari_neighbor = board_get_atari_neighbor(map->b, c, map->to_play);
			if (atari_neighbor && is_ladder(map->b, c, atari_neighbor, true)) {
				if (UDEBUGL(5))
					fprintf(stderr, "Pruning ladder move %s\n", coord2sstr(c, map->b));
				map->consider[c] = false;
			}
		} foreach_free_point_end;
	}

	if (u->prior->even_eqex)
		uct_prior_even(u, node, map);
	if (u->prior->eye_eqex)
		uct_prior_eye(u, node, map);
	if (u->prior->ko_eqex)
		uct_prior_ko(u, node, map);
	if (u->prior->b19_eqex)
		uct_prior_b19(u, node, map);
	if (u->prior->policy_eqex)
		uct_prior_playout(u, node, map);
	if (u->prior->cfgd_eqex)
		uct_prior_cfgd(u, node, map);
	if (u->prior->joseki_eqex)
		uct_prior_joseki(u, node, map);
	if (u->prior->pattern_eqex)
		uct_prior_pattern(u, node, map);
	if (u->prior->plugin_eqex)
		plugin_prior(u->plugins, node, map, u->prior->plugin_eqex);
}

struct uct_prior *
uct_prior_init(char *arg, struct board *b, struct uct *u)
{
	struct uct_prior *p = calloc2(1, sizeof(struct uct_prior));

	p->even_eqex = p->policy_eqex = p->b19_eqex = p->eye_eqex = p->ko_eqex = p->plugin_eqex = -100;
	/* FIXME: Optimal pattern_eqex is about -1000 with small playout counts
	 * but only -400 on a cluster. We need a better way to set the default
	 * here. */
	p->pattern_eqex = -800;
	p->joseki_eqex = -200;
	p->cfgdn = -1;

	/* Even number! */
	p->eqex = board_large(b) ? 20 : 14;

	p->prune_ladders = true;

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

			/* In the following settings, you can use negative
			 * numbers to give the hundredths of default eqex.
			 * E.g. -100 is default eqex, -50 is half of the
			 * default eqex, -200 is double the default eqex. */
			} else if (!strcasecmp(optname, "even") && optval) {
				p->even_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "policy") && optval) {
				p->policy_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "b19") && optval) {
				p->b19_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "cfgd") && optval) {
				/* cfgd=3%40%20%20 - 3 levels; immediate libs
				 * of last move => 40 wins, their neighbors
				 * 20 wins, 2nd-level neighbors 20 wins;
				 * neighbors are group-transitive. */
				p->cfgdn = atoi(optval); optval += strcspn(optval, "%");
				p->cfgd_eqex = calloc2(p->cfgdn + 1, sizeof(*p->cfgd_eqex));
				p->cfgd_eqex[0] = 0;
				int i;
				for (i = 1; *optval; i++, optval += strcspn(optval, "%")) {
					optval++;
					p->cfgd_eqex[i] = atoi(optval);
				}
				if (i != p->cfgdn + 1) {
					fprintf(stderr, "uct: Missing prior cfdn level %d/%d\n", i, p->cfgdn);
					exit(1);
				}

			} else if (!strcasecmp(optname, "joseki") && optval) {
				p->joseki_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "eye") && optval) {
				p->eye_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "ko") && optval) {
				p->ko_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "pattern") && optval) {
				/* Pattern-based prior eqex. */
				/* Note that this prior is still going to be
				 * used only if you have downloaded or
				 * generated the pattern files! */
				p->pattern_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "plugin") && optval) {
				/* Unlike others, this is just a *recommendation*. */
				p->plugin_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prune_ladders")) {
				p->prune_ladders = !optval || atoi(optval);
			} else {
				fprintf(stderr, "uct: Invalid prior argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	if (p->even_eqex < 0) p->even_eqex = p->eqex * -p->even_eqex / 100;
	if (p->policy_eqex < 0) p->policy_eqex = p->eqex * -p->policy_eqex / 100;
	if (p->b19_eqex < 0) p->b19_eqex = p->eqex * -p->b19_eqex / 100;
	if (p->eye_eqex < 0) p->eye_eqex = p->eqex * -p->eye_eqex / 100;
	if (p->ko_eqex < 0) p->ko_eqex = p->eqex * -p->ko_eqex / 100;
	if (p->joseki_eqex < 0) p->joseki_eqex = p->eqex * -p->joseki_eqex / 100;
	if (p->pattern_eqex < 0) p->pattern_eqex = p->eqex * -p->pattern_eqex / 100;
	if (p->plugin_eqex < 0) p->plugin_eqex = p->eqex * -p->plugin_eqex / 100;

	if (p->cfgdn < 0) {
		static int large_bonuses[] = { 0, 55, 50, 15 };
		static int small_bonuses[] = { 0, 45, 40, 15 };
		p->cfgdn = 3;
		p->cfgd_eqex = calloc2(p->cfgdn + 1, sizeof(*p->cfgd_eqex));
		memcpy(p->cfgd_eqex, board_large(b) ? large_bonuses : small_bonuses, sizeof(large_bonuses));
	}
	if (p->cfgdn > TREE_NODE_D_MAX) {
		fprintf(stderr, "uct: CFG distances only up to %d available\n", TREE_NODE_D_MAX);
		exit(1);
	}

	if (p->pattern_eqex)
		u->want_pat = true;

	return p;
}

void
uct_prior_done(struct uct_prior *p)
{
	assert(p->cfgd_eqex);
	free(p->cfgd_eqex);
	free(p);
}
