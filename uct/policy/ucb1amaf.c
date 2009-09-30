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
	float explore_p_rave;
	int equiv_rave;
	bool rave_prior, both_colors;
	bool check_nakade;
};


struct tree_node *ucb1_choose(struct uct_policy *p, struct tree_node *node, struct board *b, enum stone color);

struct tree_node *ucb1_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass);


static inline float fast_sqrt(int x)
{
	static const float table[] = {
		0,
		1,
		1.41421356237309504880,
		1.73205080756887729352,
		2.00000000000000000000,
	};
	//printf("sqrt %d\n", x);
	if (x < sizeof(table) / sizeof(*table)) {
		return table[x];
	} else {
		return sqrt(x);
	}
}

/* Sylvain RAVE function */
struct tree_node *
ucb1srave_descend(struct uct_policy *p, struct tree *tree, struct tree_node *node, int parity, bool allow_pass)
{
	struct ucb1_policy_amaf *b = p->data;
	float rave_coef = 1.0f / b->equiv_rave;
	float nconf = 1.f, rconf = 1.f;
	if (b->explore_p > 0)
		nconf = sqrt(log(node->u.playouts + node->prior.playouts));
	if (b->explore_p_rave > 0 && node->amaf.playouts)
		rconf = sqrt(log(node->amaf.playouts + node->prior.playouts));

	// XXX: Stack overflow danger on big boards?
	struct tree_node *nbest[512] = { node->children }; int nbests = 1;
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
		if (tree_parity(tree, parity) < 0) {
			nwins = ngames - nwins;
			rwins = rgames - rwins;
		}
		float nval = 0, rval = 0;
		if (ngames) {
			nval = (float) nwins / ngames;
			if (b->explore_p > 0)
				nval += b->explore_p * nconf / fast_sqrt(ngames);
		}
		if (rgames) {
			rval = (float) rwins / rgames;
			if (b->explore_p_rave > 0 && !is_pass(ni->coord))
				rval += b->explore_p_rave * rconf / fast_sqrt(rgames);
		}

		/* XXX: We later compare urgency with best_urgency; this can
		 * be difficult given that urgency can be in register with
		 * higher precision than best_urgency, thus even though
		 * the numbers are in fact the same, urgency will be
		 * slightly higher (or lower). Thus, we declare urgency
		 * as volatile, attempting to force the compiler to keep
		 * everything as a float. Ideally, we should do some random
		 * __FLT_EPSILON__ magic instead. */
		volatile float urgency;
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
			/* assert(!u->even_eqex); */
			urgency = b->fpu;
		}

#if 0
		struct board bb; bb.size = 11;
		//if (node->coord == 7*11+4) // D7
		fprintf(stderr, "%s<%lld>-%s<%lld> urgency %f (r %d / %d, n %d / %d)\n",
			coord2sstr(ni->parent->coord, &bb), ni->parent->hash,
			coord2sstr(ni->coord, &bb), ni->hash, urgency,
			rwins, rgames, nwins, ngames);
#endif
		if (b->urg_randoma)
			urgency += (float)(fast_random(b->urg_randoma) - b->urg_randoma / 2) / 1000;
		if (b->urg_randomm)
			urgency *= (float)(fast_random(b->urg_randomm) + 5) / b->urg_randomm;

		if (urgency > best_urgency) {
			best_urgency = urgency; nbests = 0;
		}
		if (urgency >= best_urgency) {
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
	fprintf(stderr, "[%s %d: ", coord2sstr(node->coord, &bb), nbests);
	for (int zz = 0; zz < nbests; zz++)
		fprintf(stderr, "%s", coord2sstr(nbest[zz]->coord, &bb));
	fprintf(stderr, "]\n");
#endif
	return nbest[fast_random(nbests)];
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

		if (p->descend != ucb1_descend)
			node->hints |= NODE_HINT_NOAMAF; /* Rave, different update function */
		update_node(p, node, result);
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

			if (p->descend != ucb1_descend)
				ni->hints |= NODE_HINT_NOAMAF; /* Rave, different update function */
			update_node_amaf(p, ni, nres);

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
	p->descend = ucb1srave_descend;
	p->choose = ucb1_choose;
	p->update = ucb1amaf_update;
	p->wants_amaf = true;

	// RAVE: 0.2vs0: 40% (+-7.3) 0.1vs0: 54.7% (+-3.5)
	b->explore_p = 0.1;
	b->explore_p_rave = 0.01;
	b->equiv_rave = 3000;
	b->fpu = INFINITY;
	b->rave_prior = true;
	b->check_nakade = true;

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
			} else if (!strcasecmp(optname, "rave")) {
				if (optval && *optval == '0')
					p->descend = ucb1_descend;
				else if (optval && *optval == 's')
					p->descend = ucb1srave_descend;
			} else if (!strcasecmp(optname, "explore_p_rave") && optval) {
				b->explore_p_rave = atof(optval);
			} else if (!strcasecmp(optname, "equiv_rave") && optval) {
				b->equiv_rave = atof(optval);
			} else if (!strcasecmp(optname, "rave_prior") && optval) {
				// 46% (+-3.5)
				b->rave_prior = atoi(optval);
			} else if (!strcasecmp(optname, "both_colors")) {
				b->both_colors = true;
			} else if (!strcasecmp(optname, "check_nakade")) {
				b->check_nakade = !optval || *optval == '1';
			} else {
				fprintf(stderr, "ucb1: Invalid policy argument %s or missing value\n", optname);
			}
		}
	}

	if (b->explore_p_rave < 0) b->explore_p_rave = b->explore_p;

	return p;
}
