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
#include "uct/plugins.h"
#include "uct/prior.h"
#include "uct/tree.h"

/* Plugin interface for UCT. External plugins may hook callbacks on various
 * events and e.g. bias the tree. */


/* Keep the API typedefs in sync with <uct/plugin.h>. */

struct plugin {
	char *path;
	char *args;
	void *dlh;
	void *data;

	void *(*init)(char *args, struct board *b, int seed);
	void (*prior)(void *data, struct tree_node *node, struct prior_map *map, int eqex);
	void (*done)(void *data);
};

struct uct_pluginset {
	struct plugin *plugins;
	int n_plugins;
	struct board *b;
};


#ifdef WIN32

/* We do not support plugins on Windows. Minimal dummy stubs. */

struct uct_pluginset *
pluginset_init(struct board *b)
{
	return NULL;
}
void
pluginset_done(struct uct_pluginset *ps)
{
	assert(!ps);
}
void
plugin_load(struct uct_pluginset *ps, char *path, char *args)
{
	assert(!ps);
}
void
plugin_prior(struct uct_pluginset *ps, struct tree_node *node, struct prior_map *map, int eqex)
{
	assert(!ps);
}

#else

struct uct_pluginset *
pluginset_init(struct board *b)
{
	struct uct_pluginset *ps = calloc(1, sizeof(*ps));
	ps->b = b;
	return ps;
}

void
pluginset_done(struct uct_pluginset *ps)
{
	for (int i = 0; i < ps->n_plugins; i++) {
		struct plugin *p = &ps->plugins[i];
		p->done(p->data);
		dlclose(p->dlh);
		free(p->path);
		free(p->args);
	}
	free(ps);
}


void
plugin_load(struct uct_pluginset *ps, char *path, char *args)
{
	ps->plugins = realloc(ps->plugins, ++ps->n_plugins * sizeof(ps->plugins[0]));
	struct plugin *p = &ps->plugins[ps->n_plugins - 1];
	p->path = strdup(path);
	p->args = args ? strdup(args) : args;

	p->dlh = dlopen(path, RTLD_NOW);
	if (!p->dlh) {
		fprintf(stderr, "Cannot load plugin %s: %s\n", path, dlerror());
		exit(EXIT_FAILURE);
	}
#define loadsym(s_) do {\
	p->s_ = dlsym(p->dlh, "pachi_plugin_" #s_); \
	if (!p->s_) { \
		fprintf(stderr, "Cannot find pachi_plugin_%s in plugin %s: %s\n", #s_, path, dlerror()); \
		exit(EXIT_FAILURE); \
	} \
} while (0)
	loadsym(init);
	loadsym(prior);
	loadsym(done);

	p->data = p->init(p->args, ps->b, fast_random(65536));
}

void
plugin_prior(struct uct_pluginset *ps, struct tree_node *node, struct prior_map *map, int eqex)
{
	for (int i = 0; i < ps->n_plugins; i++) {
		struct plugin *p = &ps->plugins[i];
		p->prior(p->data, node, map, eqex);
	}
}

#endif
