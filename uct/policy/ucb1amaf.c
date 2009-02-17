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
	/* Equivalent experience for prior knowledge. MoGo paper recommends
	 * 50 playouts per source. */
	int eqex, even_eqex, gp_eqex, policy_eqex;
	int urg_randoma, urg_randomm;
	float explore_p_rave;
	int equiv_rave;
	bool rave_prior, both_colors;
};


struct tree_node *ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

struct tree_node *ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);

void ucb1_prior(struct uct_policy *p, struct tree *tree, struct tree_node *node, struct board *b, enum stone color, int parity);


/* Original RAVE function */
struct tree_node *
ucb1orave_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	/* We want to count in the prior stats here after all. Otherwise,
	 * nodes with positive prior will get explored _LESS_ since the
	 * urgency will be always higher; even with normal FPU because
	 * of the explore coefficient. */

	struct ucb1_policy_amaf *b = p->data;
	float xpl = log(node->u.playouts + node->prior.playouts) * b->explore_p;
	float xpl_rave = log(node->amaf.playouts + (b->rave_prior ? node->prior.playouts : 0)) * b->explore_p_rave;
	float beta = sqrt((float)b->equiv_rave / (3 * (node->u.playouts + node->prior.playouts) + b->equiv_rave));

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;
		int amaf_wins = ni->amaf.wins + (b->rave_prior ? ni->prior.wins : 0);
		int amaf_playouts = ni->amaf.playouts + (b->rave_prior ? ni->prior.playouts : 0);
		int uct_playouts = ni->u.playouts + ni->prior.playouts;
		ni->amaf.value = (float)amaf_wins / amaf_playouts;
		ni->prior.value = (float)ni->prior.wins / ni->prior.playouts;
		float uctp = (parity > 0 ? ni->u.value : 1 - ni->u.value) + sqrt(xpl / uct_playouts);
		float ravep = (parity > 0 ? ni->amaf.value : 1 - ni->amaf.value) + sqrt(xpl_rave / amaf_playouts);
		float urgency = ni->u.playouts ? beta * ravep + (1 - beta) * uctp : b->fpu;
		// fprintf(stderr, "uctp %f (uct %d/%d) ravep %f (xpl %f amaf %d/%d) beta %f => %f\n", uctp, ni->u.wins, ni->u.playouts, ravep, xpl_rave, amaf_wins, amaf_playouts, beta, urgency);
		if (b->urg_randoma)
			urgency += (float)(fast_random(b->urg_randoma) - b->urg_randoma / 2) / 1000;
		if (b->urg_randomm)
			urgency *= (float)(fast_random(b->urg_randomm) + 5) / b->urg_randomm;
		if (urgency > best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

float fast_sqrt(int x)
{
	static const float table[] = {
		0,
		1,
		1.41421356237309504880,
		1.73205080756887729352,
		2.00000000000000000000,
#if 0
		2.23606797749978969640,
		2.44948974278317809819,
		2.64575131106459059050,
		2.82842712474619009760,
		3.00000000000000000000,
		3.16227766016837933199,
		3.31662479035539984911,
		3.46410161513775458705,
		3.60555127546398929311,
		3.74165738677394138558,
		3.87298334620741688517,
		4.00000000000000000000,
		4.12310562561766054982,
		4.24264068711928514640,
		4.35889894354067355223,
		4.47213595499957939281,
		4.58257569495584000658,
		4.69041575982342955456,
		4.79583152331271954159,
		4.89897948556635619639,
		5.00000000000000000000,
		5.09901951359278483002,
		5.19615242270663188058,
		5.29150262212918118100,
		5.38516480713450403125,
		5.47722557505166113456,
		5.56776436283002192211,
		5.65685424949238019520,
		5.74456264653802865985,
		5.83095189484530047087,
		5.91607978309961604256,
		6.00000000000000000000,
		6.08276253029821968899,
		6.16441400296897645025,
		6.24499799839839820584,
		6.32455532033675866399,
		6.40312423743284868648,
		6.48074069840786023096,
		6.55743852430200065234,
		6.63324958071079969822,
		6.70820393249936908922,
		6.78232998312526813906,
		6.85565460040104412493,
		6.92820323027550917410,
		7.00000000000000000000,
		7.07106781186547524400,
		7.14142842854284999799,
		7.21110255092797858623,
		7.28010988928051827109,
		7.34846922834953429459,
		7.41619848709566294871,
		7.48331477354788277116,
		7.54983443527074969723,
		7.61577310586390828566,
		7.68114574786860817576,
		7.74596669241483377035,
		7.81024967590665439412,
		7.87400787401181101968,
		7.93725393319377177150,
#endif
	};
	//printf("sqrt %d\n", x);
	if (x < sizeof(table) / sizeof(*table)) {
		return table[x];
	} else {
		return sqrt(x);
#if 0
		int y = 0;
		int base = 1 << (sizeof(int) * 8 - 2);
		if ((x & 0xFFFF0000) == 0) base >>= 16;
		if ((x & 0xFF00FF00) == 0) base >>= 8;
		if ((x & 0xF0F0F0F0) == 0) base >>= 4;
		if ((x & 0xCCCCCCCC) == 0) base >>= 2;
		// "base" starts at the highest power of four <= the argument.

		while (base > 0) {
			if (x >= y + base) {
				x -= y + base;
				y += base << 1;
			}
			y >>= 1;
			base >>= 2;
		}
		printf("sqrt %d = %d\n", x, y);
		return y;
#endif
	}
}

/* Sylvain RAVE function */
struct tree_node *
ucb1srave_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	struct ucb1_policy_amaf *b = p->data;
	float rave_coef = 1.0f / b->equiv_rave;
	float conf = 1.f;
	if (b->explore_p > 0 || b->explore_p_rave > 0)
		conf = sqrt(log(node->u.playouts + node->prior.playouts));

	struct tree_node *nbest = node->children;
	float best_urgency = -9999;
	for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
		/* Do not consider passing early. */
		if (likely(!allow_pass) && unlikely(is_pass(ni->coord)))
			continue;

		/* TODO: Exploration? */

		int ngames = ni->u.playouts;
		int nwins = ni->u.wins;
		int rgames = ni->amaf.playouts;
		int rwins = ni->amaf.wins;
		if (b->rave_prior) {
			rgames += ni->prior.playouts;
			rwins += ni->prior.wins;
		} else {
			ngames += ni->prior.playouts;
			nwins += ni->prior.wins;
		}
		if (parity < 0) {
			nwins = ngames - nwins;
			rwins = rgames - rwins;
		}
		float nval = 0, rval = 0;
		if (ngames) {
			nval = (float) nwins / ngames;
			if (b->explore_p > 0)
				nval += b->explore_p * conf / fast_sqrt(ngames);
		}
		if (rgames) {
			rval = (float) rwins / rgames;
			if (b->explore_p_rave > 0)
				rval += b->explore_p_rave * conf / fast_sqrt(rgames);
		}

		float urgency;
		if (ngames) {
			if (rgames) {
				/* At the beginning, beta is at 1 and RAVE is used.
				 * At b->equiv_rate, beta is at 1/3 and gets steeper on. */
				float beta = (float) rgames / (rgames + ngames + rave_coef * ngames * rgames);
#if 0
				//if (node->coord == 7*11+4) // D7
				fprintf(stderr, "[beta %f = %d / (%d + %d + %f)]\n",
					beta, rgames, rgames, ngames, rave_coef * ngames * rgames);
#endif
				urgency = beta * rval + (1 - beta) * nval;
			} else {
				urgency = nval;
			}
		} else if (rgames) {
			urgency = rval;
		} else {
			assert(!b->even_eqex);
			urgency = b->fpu;
		}

#if 0
		struct board bb; bb.size = 11;
		//if (node->coord == 7*11+4) // D7
		fprintf(stderr, "%s<%lld> urgency %f (r %d / %d, n %d / %d)\n",
			coord2sstr(ni->coord, &bb), ni->hash, urgency, rwins, rgames, nwins, ngames);
#endif
		if (b->urg_randoma)
			urgency += (float)(fast_random(b->urg_randoma) - b->urg_randoma / 2) / 1000;
		if (b->urg_randomm)
			urgency *= (float)(fast_random(b->urg_randomm) + 5) / b->urg_randomm;
		/* The >= is important since we will always choose something
		 * else than a pass in case of a tie. pass causes degenerative
		 * behaviour. */
		if (urgency >= best_urgency) {
			best_urgency = urgency;
			nbest = ni;
		}
	}
	return nbest;
}

static void
update_node(struct uct_policy *p, struct tree_node *node, int result)
{
	node->u.playouts++;
	node->u.wins += result;
	tree_update_node_value(node);
}
static void
update_node_amaf(struct uct_policy *p, struct tree_node *node, int result)
{
	node->amaf.playouts++;
	node->amaf.wins += result;
	tree_update_node_value(node);
}

void
ucb1amaf_update(struct uct_policy *p, struct tree *tree, struct tree_node *node, enum stone color, struct playout_amafmap *map, int result)
{
	struct ucb1_policy_amaf *b = p->data;

	color = stone_other(color); // We will look in CHILDREN of the node!
	for (; node; node = node->parent) {
		if (p->descend != ucb1_descend)
			node->hints |= NODE_HINT_NOAMAF; /* Rave, different update function */
		update_node(p, node, result);
		if (amaf_nakade(map->map[node->coord]))
			amaf_op(map->map[node->coord], -);
		/* This loop ignores symmetry considerations, but they should
		 * matter only at a point when AMAF doesn't help much. */
		for (struct tree_node *ni = node->children; ni; ni = ni->sibling) {
			assert(map->map[ni->coord] != S_OFFBOARD);
			if (map->map[ni->coord] == S_NONE || amaf_nakade(map->map[ni->coord]))
				continue;

#if 0
			struct board bb; bb.size = 9+2;
			fprintf(stderr, "%s<%lld> -> %s<%lld> [%d %d => %d]\n", coord2sstr(node->coord, &bb), node->hash, coord2sstr(ni->coord, &bb), ni->hash, map->map[ni->coord], color, result);
#endif
			if (p->descend != ucb1_descend)
				ni->hints |= NODE_HINT_NOAMAF; /* Rave, different update function */
			if (b->both_colors) {
				update_node_amaf(p, ni, map->map[ni->coord] == color ? result : !result);
			} else if (map->map[ni->coord] == color) {
				update_node_amaf(p, ni, result);
			}
		}
	}
}


struct uct_policy *
policy_ucb1amaf_init(struct uct *u, char *arg)
{
	struct uct_policy *p = calloc(1, sizeof(*p));
	struct ucb1_policy_amaf *b = calloc(1, sizeof(*b));
	p->uct = u;
	p->data = b;
	p->descend = ucb1srave_descend;
	p->choose = ucb1_choose;
	p->update = ucb1amaf_update;
	p->wants_amaf = true;

	// RAVE: 0.2vs0: 40% (+-7.3) 0.1vs0: 54.7% (+-3.5)
	b->explore_p = 0.1;
	b->explore_p_rave = -1;
	b->equiv_rave = 3000;
	b->fpu = INFINITY;
	// gp: 14 vs 0: 44% (+-3.5)
	b->gp_eqex = 0;
	b->even_eqex = b->policy_eqex = -1;
	b->eqex = 8;

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
			} else if (!strcasecmp(optname, "prior")) {
				if (optval)
					b->eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prior_even") && optval) {
				b->even_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prior_gp") && optval) {
				b->gp_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "prior_policy") && optval) {
				b->policy_eqex = atoi(optval);
			} else if (!strcasecmp(optname, "fpu") && optval) {
				b->fpu = atof(optval);
			} else if (!strcasecmp(optname, "urg_randoma") && optval) {
				b->urg_randoma = atoi(optval);
			} else if (!strcasecmp(optname, "urg_randomm") && optval) {
				b->urg_randomm = atoi(optval);
			} else if (!strcasecmp(optname, "rave")) {
				if (optval && *optval == '0')
					p->descend = ucb1_descend;
				else if (optval && *optval == 'o')
					p->descend = ucb1orave_descend;
				else if (optval && *optval == 's')
					p->descend = ucb1srave_descend;
			} else if (!strcasecmp(optname, "explore_p_rave") && optval) {
				b->explore_p_rave = atof(optval);
			} else if (!strcasecmp(optname, "equiv_rave") && optval) {
				b->equiv_rave = atof(optval);
			} else if (!strcasecmp(optname, "rave_prior")) {
				// 46% (+-3.5)
				b->rave_prior = true;
			} else if (!strcasecmp(optname, "both_colors")) {
				b->both_colors = true;
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	if (b->eqex) p->prior = ucb1_prior;
	if (b->even_eqex < 0) b->even_eqex = b->eqex;
	if (b->gp_eqex < 0) b->gp_eqex = b->eqex;
	if (b->policy_eqex < 0) b->policy_eqex = b->eqex;
	if (b->explore_p_rave < 0) b->explore_p_rave = b->explore_p;

	return p;
}
