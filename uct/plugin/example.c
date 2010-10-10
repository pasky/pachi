/* This is an example Pachi UCT plugin. */
/* This file is released under the same licence conditions as
 * <uct/plugin.h>. */

/* We will add positive priors (1.0) for moves that play in-between
 * of two different groups of the same color; that is, moves that connect
 * two groups or the same color or separate two groups of the same color.
 * This is not a very good prior actually, since it leads to a lot of
 * useless moves. (Maybe doing this in simulations would be more interesting?)
 * But it is a simple enough example. :-) */

/* Compile the plugin like this:
 * gcc -Wall -O3 -march=native -Ipachi_source_root -shared -fPIC -o example.so example.c
 * Then, load it in Pachi by passing plugin=example.so as a parameter.
 * You can also pass it parameters: plugin=example.so:p1=v1:p2=v2.
 * The module supports these parameters:
 * eqex		Number of prior'd simulations, overrides Pachi default
 * selfatari	If specified (selfatari or selfatari=1), test for selfatari
 *              before giving the prior
 */

#include <stdio.h>
#include <stdlib.h>

/* The basic plugin interface. */
#include "uct/plugin.h"
/* The tactical reading tools, for selfatari testing. */
#include "tactics/selfatari.h"

/* Our context structure. */
struct context {
	int eqex;
	bool selfatari;
};


void
pachi_plugin_prior(void *data, struct tree_node *node, struct prior_map *map, int eqex)
{
	struct context *ctx = data;
	struct board *b = map->b;
	if (ctx->eqex >= 0)
		eqex = ctx->eqex; // override Pachi default

	/* foreach_free_point defines a variable @c corresponding
	 * to our current coordinate. */
	foreach_free_point(map->b) {
		if (!map->consider[c])
			continue;

		/* We will look at the current point's 4-neighborhood;
		 * we are to set a prior if we spot two different
		 * groups of the same color. */

		/* First, a shortcut: We keep track of numbers of neighboring
		 * stones. The | is not a typo. */
		if ((neighbor_count_at(b, c, S_BLACK) | neighbor_count_at(b, c, S_WHITE)) <= 1)
			continue;

		/* Keep track of seen groups for each color; at each
		 * point, we will look only at groups with the same color. */
		int groups[S_MAX] = {0, 0, 0, 0};

		/* foreach_neighbor goes through all direct neighbors
		 * of a given coordinate defines also its own variable @c
		 * corresponding to the current coordinate. */
		foreach_neighbor(b, c, {
			enum stone s = board_at(b, c); // color of stone
			group_t g = group_at(b, c); // id of a group
			if (!g) continue; // no group at this coord

			if (!groups[s]) {
				/* First time we see a group of this color. */
				groups[s] = g;
				continue;
			}
			if (groups[s] == g) {
				/* We have already seen this group. */
				continue;
			}
			/* We have already seen another group of this color!
			 * We can connect or split. */
			goto set_prior;
		});
		/* If we reach this point, we have not seen any two groups
		 * to connect. */
		continue;

set_prior:
		/* Check if our move here is not self-atari if the option
		 * is enabled. */
		if (ctx->selfatari && is_bad_selfatari(b, map->to_play, c))
			continue;

		/* Finally record the prior; value is 0.0 (avoid) to 1.0
		 * (strongly favor). eqex is the number of simulations
		 * the value is worth. */
		add_prior_value(map, c, 1.0, eqex);
	} foreach_free_point_end;
}

void *
pachi_plugin_init(char *arg, struct board *b, int seed)
{
	struct context *ctx = calloc(1, sizeof(*ctx));

	/* Initialize ctx defaults here. */
	ctx->eqex = -1;

	/* This is the canonical Pachi arguments parser. You do not strictly
	 * need to decypher it, you can just use it as a boilerplate. */
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
				/* eqex takes a required integer argument */
				ctx->eqex = atoi(optval);

			} else if (!strcasecmp(optname, "selfatari")) {
				/* selfatari takes an optional integer
				 * argument */
				ctx->selfatari = !optval || atoi(optval);

			} else {
				fprintf(stderr, "example plugin: Invalid argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	/* Initialize the rest of ctx (depending on arguments) here. */

	return ctx;
}

void
pachi_plugin_done(void *data)
{
	struct context *ctx = data;
	/* No big magic. */
	free(ctx);
}
