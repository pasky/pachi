#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "uct/internal.h"
#include "uct/tree.h"

/* This implements the UCB1 policy with an extra AMAF heuristics. */

struct ucb1_policy_amaf {
	/* This is what the Modification of UCT with Patterns in Monte Carlo Go
	 * paper calls 'p'. Original UCB has this on 2, but this seems to
	 * produce way too wide searches; reduce this to get deeper and
	 * narrower readouts - try 0.2. */
	float explore_p;
	/* First Play Urgency - if set to less than infinity (the MoGo paper
	 * above reports 1.0 as the best), new branches are explored only
	 * if none of the existing ones has higher urgency than fpu. */
	float fpu;
	int urg_randoma, urg_randomm;
	int equiv_rave;
	bool both_colors;
	bool check_nakade;
	bool sylvain_rave;
};


struct tree_node *ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

struct tree_node *ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);


static inline float fast_sqrt(int x)
{
	static const float table[] = {
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
	//printf("sqrt %d\n", x);
	if (x < sizeof(table) / sizeof(*table)) {
		return table[x];
	} else {
		return sqrt(x);
	}
}

struct tree_node *
ucb1rave_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	struct ucb1_policy_amaf *b = p->data;
	float beta = 0;
	float nconf = 1.f;
	if (b->explore_p > 0)
		nconf = sqrt(log(node->u.playouts + node->prior.playouts));

	if (!b->sylvain_rave)
		beta = sqrt(b->equiv_rave / (3 * node->u.playouts + b->equiv_rave));

	// XXX: Stack overflow danger on big boards?
	struct tree_node *nbest[512] = { node->children }; int nbests = 1;
	float best_urgency = -9999;

	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (unlikely(!allow_pass && is_pass(ni->coord)))
			continue;

		/* TODO: Exploration? */

		struct move_stats n = ni->u, r = ni->amaf;
		if (p->uct->amaf_prior) {
			stats_merge(&r, &ni->prior);
		} else {
			stats_merge(&n, &ni->prior);
		}
		if (tree_parity(tree, parity) < 0) {
			stats_reverse_parity(&n);
			stats_reverse_parity(&r);
		}

		float urgency;
		if (n.playouts) {
			if (r.playouts) {
				/* At the beginning, beta is at 1 and RAVE is used.
				 * At b->equiv_rate, beta is at 1/3 and gets steeper on. */
				if (b->sylvain_rave)
					beta = (float) r.playouts / (r.playouts + n.playouts + n.playouts * r.playouts / b->equiv_rave);
#if 0
				//if (node->coord == 7*11+4) // D7
				fprintf(stderr, "[beta %f = %d / (%d + %d + %f)]\n",
					beta, rgames, rgames, ngames, ngames * rgames / b->equiv_rave);
#endif
				urgency = beta * r.value + (1.f - beta) * n.value;
			} else {
				urgency = n.value;
			}

			if (b->explore_p > 0)
				urgency += b->explore_p * nconf / fast_sqrt(n.playouts);
		} else if (r.playouts) {
			urgency = r.value;
		} else {
			/* assert(!u->even_eqex); */
			urgency = b->fpu;
		}

#if 0
		struct board bb; bb.size = 11;
		//if (node->coord == 7*11+4) // D7
		fprintf(stderr, "%s<%lld>-%s<%lld> urgency %f (r %d / %d + e = %f, n %d / %d + e = %f)\n",
			coord2sstr(ni->parent->coord, &bb), ni->parent->hash,
			coord2sstr(ni->coord, &bb), ni->hash, urgency,
			rwins, rgames, rval, nwins, ngames, nval);
#endif
		if (b->urg_randoma)
			urgency += (float)(fast_random(b->urg_randoma) - b->urg_randoma / 2) / 1000;
		if (b->urg_randomm)
			urgency *= (float)(fast_random(b->urg_randomm) + 5) / b->urg_randomm;

		if (urgency - best_urgency > __FLT_EPSILON__) { // urgency > best_urgency
			best_urgency = urgency; nbests = 0;
		}
		if (urgency - best_urgency > -__FLT_EPSILON__) { // urgency >= best_urgency
			/* We want to always choose something else than a pass
			 * in case of a tie. pass causes degenerative behaviour. */
			if (nbests == 1 && is_pass(nbest[0]->coord)) {
				nbests--;
			}
			nbest[nbests++] = ni;
		}
	}
#if 0
	struct board bb; bb.size = 11;
	fprintf(stderr, "RESULT [%s %d: ", coord2sstr(node->coord, &bb), nbests);
	for (int zz = 0; zz < nbests; zz++)
		fprintf(stderr, "%s", coord2sstr(nbest[zz]->coord, &bb));
	fprintf(stderr, "]\n");
#endif
	return nbest[fast_random(nbests)];
}

void
ucb1amaf_update(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone node_color, enum stone player_color, struct playout_amafmap *map, int result)
{
	struct ucb1_policy_amaf *b = p->data;
	enum stone child_color = stone_other(node_color);

#if 0
	struct board bb; bb.size = 9+2;
	for (struct tree_node *ni = node; ni; ni = ni->parent)
		fprintf(stderr, "%s ", coord2sstr(ni->coord, &bb));
	fprintf(stderr, "[color %d] update result %d (color %d)\n",
			node_color, result, player_color);
#endif

	while (node) {
		if (node->parent == NULL)
			assert(tree->root_color == stone_other(child_color));

		stats_add_result(&node->u, result, 1);
		if (amaf_nakade(map->map[node->coord]))
			amaf_op(map->map[node->coord], -);

		/* This loop ignores symmetry considerations, but they should
		 * matter only at a point when AMAF doesn't help much. */
		for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
			assert(map->map[ni->coord] != S_OFFBOARD);
			if (map->map[ni->coord] == S_NONE)
				continue;
			assert(map->game_baselen >= 0);
			enum stone amaf_color = map->map[ni->coord];
			if (amaf_nakade(map->map[ni->coord])) {
				if (!b->check_nakade)
					continue;
				/* We don't care to implement both_colors
				 * properly since it sucks anyway. */
				int i;
				for (i = map->game_baselen; i < map->gamelen; i++)
					if (map->game[i].coord == ni->coord
					    && map->game[i].color == child_color)
						break;
				if (i == map->gamelen)
					continue;
				amaf_color = child_color;
			}

			int nres = result;
			if (amaf_color != child_color) {
				if (!b->both_colors)
					continue;
				nres = !nres;
			}
			/* For child_color != player_color, we still want
			 * to record the result unmodified; in that case,
			 * we will correctly negate them at the descend phase. */

			stats_add_result(&node->amaf, nres, 1);

#if 0
			fprintf(stderr, "* %s<%lld> -> %s<%lld> [%d %d => %d/%d]\n", coord2sstr(node->coord, &bb), node->hash, coord2sstr(ni->coord, &bb), ni->hash, player_color, child_color, result);
#endif
		}

		if (!is_pass(node->coord)) {
			map->game_baselen--;
		}
		node = node->parent; child_color = stone_other(child_color);
	}
}


struct uct_policy *
policy_ucb1amaf_init(struct uct *u, char *arg)
{
	struct uct_policy *p = calloc(1, sizeof(*p));
	struct ucb1_policy_amaf *b = calloc(1, sizeof(*b));
	p->uct = u;
	p->data = b;
	p->descend = ucb1rave_descend;
	p->choose = ucb1_choose;
	p->update = ucb1amaf_update;
	p->wants_amaf = true;

	// RAVE: 0.2vs0: 40% (+-7.3) 0.1vs0: 54.7% (+-3.5)
	b->explore_p = 0.1;
	b->equiv_rave = 3000;
	b->fpu = INFINITY;
	b->check_nakade = true;
	b->sylvain_rave = true;

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
			} else if (!strcasecmp(optname, "urg_randoma") && optval) {
				b->urg_randoma = atoi(optval);
			} else if (!strcasecmp(optname, "urg_randomm") && optval) {
				b->urg_randomm = atoi(optval);
			} else if (!strcasecmp(optname, "equiv_rave") && optval) {
				b->equiv_rave = atof(optval);
			} else if (!strcasecmp(optname, "both_colors")) {
				b->both_colors = true;
			} else if (!strcasecmp(optname, "sylvain_rave")) {
				b->sylvain_rave = !optval || *optval == '1';
			} else if (!strcasecmp(optname, "check_nakade")) {
				b->check_nakade = !optval || *optval == '1';
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	return p;
}
