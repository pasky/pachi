#ifndef PACHI_UCT_PLUGIN_H
#define PACHI_UCT_PLUGIN_H

/* This is the public API for external Pachi UCT plugins. */
/* Unlike the rest of Pachi, this file is available for anyone for
 * unrestricted use and distribution. The plugin interface is not
 * restricted by the terms of GPL and plugins may have any arbitrary
 * licence conditions and do not need to be GPL or open-source at all. */

/* Contrast this with <uct/plugins.h>, which is internal Pachi API
 * for calling all loaded modules. */

/* In order to compile your plugin, pass cc an -I parameter pointing
 * at the root Pachi sources directory and include this file - it should
 * pull in everything else neccessary. */

/* No API stability guarantees can be made at this point. The board
 * structure and UCT tree in particular _are_ likely to change again. */

#include <assert.h> // we use assertions a lot, even in .h files

#include "stone.h" // stones and their colors
#include "move.h" // coordinates and (coordinate, stone) pairs ("moves")
#include "board.h" // the goban structure and basic interface
#include "uct/prior.h" // the UCT tree prior values assignment
#include "uct/tree.h" // the UCT minimax tree and node structures


/* This function is called at the time a new game is started (more precisely,
 * when clear_board GTP command is received; board size is already known and
 * the board structure is initialized, but komi is not known yet), with
 * argument string passed on Pachi's commandline. The pointer the function
 * returns is assumed to be the plugin's context and will be passed to all
 * subsequent functions called within the game.
 *
 * The seed is a random seed in the range of 0..65335. If the plugin will use
 * fast_random() from Pachi's <random.h>, this should be ignored; otherwise,
 * if the plugin is using a random generator, it should be seeded with this
 * value so that Pachi would play the same game with the same random seed.
 *
 * When the game finishes and new game is started, the current context is
 * deinitialized and the init function is called again. The game is monotonic;
 * no moves are undone when made (in case of undo, the game is cancelled and
 * re-played from beginning). */
void *pachi_plugin_init(char *args, struct board *b, int seed);

/* This function is called when priors are to be assigned to all leaves
 * of a given node. Usually, the leaves have been freshly expanded (but in
 * theory, this may be also a delayed bias). Eqex is a recommendation on how
 * many simulations should the prior information be worth.
 *
 * The function should evaluate the board situation when in tree node @node
 * and save evaluation of various coordinates to @map. The @map structure
 * contains a lot of useful information; map->b is the board in the @node
 * situation, map->to_play is who is to play, map->consider is bool array
 * describing which coordinates are worth evaluating at all and map->distances
 * are pre-computed Common Fate Graph distances from the last move. To record
 * priors, use add_prior_value(). See <uct/prior.h> for details and the
 * <uct/prior.c> for the default prior evaluation functions. */
void pachi_plugin_prior(void *data, struct tree_node *node, struct prior_map *map, int eqex);

/* This function is called when the game has ended and the context needs
 * to be deinitialized. */
void pachi_plugin_done(void *data);

#endif
