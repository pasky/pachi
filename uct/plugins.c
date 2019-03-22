#include <assert.h>
#ifndef WIN32
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "random.h"
#include "uct/prior.h"
#include "uct/tree.h"
#include "uct/plugins.h"

/* Plugin interface for UCT. External plugins may hook callbacks on various
 * events and e.g. bias the tree. */


/* Keep the API typedefs in sync with <uct/plugin.h>. */

typedef void *(*plugin_init_t)(char *args, board_t *b, int seed);
typedef	void (*plugin_prior_t)(void *data, tree_node_t *node, prior_map_t *map, int eqex);
typedef	void (*plugin_done_t)(void *data);

typedef struct {
	char *path;
	char *args;
	void *dlh;
	void *data;

	plugin_init_t  init;
	plugin_prior_t prior;
	plugin_done_t  done;
} plugin_t;

typedef struct uct_pluginset {
	plugin_t *plugins;
	int n_plugins;
	board_t *b;
} uct_pluginset_t;


uct_pluginset_t *
pluginset_init(board_t *b)
{
	uct_pluginset_t *ps = calloc2(1, uct_pluginset_t);
	ps->b = b;
	return ps;
}

void
pluginset_done(uct_pluginset_t *ps)
{
	for (int i = 0; i < ps->n_plugins; i++) {
		plugin_t *p = &ps->plugins[i];
		p->done(p->data);
		dlclose(p->dlh);
		free(p->path);
		free(p->args);
	}
	free(ps);
}


void
plugin_load(uct_pluginset_t *ps, char *path, char *args)
{
	ps->plugins = (plugin_t*)realloc(ps->plugins, ++ps->n_plugins * sizeof(ps->plugins[0]));
	plugin_t *p = &ps->plugins[ps->n_plugins - 1];
	p->path = strdup(path);
	p->args = args ? strdup(args) : args;

	p->dlh = dlopen(path, RTLD_NOW);
	if (!p->dlh)
		die("Cannot load plugin %s: %s\n", path, dlerror());
#define loadsym(s_, type) do {			    \
	p->s_ = (type)dlsym(p->dlh, "pachi_plugin_" #s_);	\
	if (!p->s_) \
		die("Cannot find pachi_plugin_%s in plugin %s: %s\n", #s_, path, dlerror()); \
} while (0)
	loadsym(init, plugin_init_t);
	loadsym(prior, plugin_prior_t);
	loadsym(done, plugin_done_t);

	p->data = p->init(p->args, ps->b, fast_random(65536));
}

void
plugin_prior(uct_pluginset_t *ps, tree_node_t *node, prior_map_t *map, int eqex)
{
	for (int i = 0; i < ps->n_plugins; i++) {
		plugin_t *p = &ps->plugins[i];
		p->prior(p->data, node, map, eqex);
	}
}

