#ifndef PACHI_UCT_PLUGINS_H
#define PACHI_UCT_PLUGINS_H


/* The pluginset structure of current UCT context. */
typedef struct uct_pluginset uct_pluginset_t;
uct_pluginset_t *pluginset_init(board_t *b);
void pluginset_done(uct_pluginset_t *ps);

/* Load a new plugin with DLL at path, passed arguments in args. */
void plugin_load(uct_pluginset_t *ps, char *path, char *args);

/* Query plugins for priors of a node's leaves. */
void plugin_prior(uct_pluginset_t *ps, tree_node_t *node, prior_map_t *map, int eqex);

#endif
