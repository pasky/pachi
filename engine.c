#define DEBUG
#include <unistd.h>

#include "engine.h"
#include "debug.h"
#include "pachi.h"

/* engine headers */
#include "uct/uct.h"
#include "distributed/distributed.h"
#include "engines/replay.h"
#include "engines/montecarlo.h"
#include "engines/random.h"
#include "engines/external.h"
#include "dcnn/dcnn_engine.h"
#include "dcnn/blunderscan.h"
#include "pattern/patternscan.h"
#include "pattern/pattern_engine.h"
#include "joseki/joseki_engine.h"
#include "joseki/josekiload.h"
#include "josekifix/josekifix_engine.h"
#include "josekifix/josekifixload.h"
#include "josekifix/josekifixscan.h"


/**************************************************************************************************/
/* Engines list */

typedef struct {
	int		id;
	char	       *name;
	engine_init_t	init;
	bool		show;
} engine_map_t;

/* Must match order in engine.h */
engine_map_t engines[] = {
	{ E_UCT,		"uct",            uct_engine_init,            1 },
#ifdef DCNN
	{ E_DCNN,		"dcnn",           dcnn_engine_init,           1 },
#ifdef EXTRA_ENGINES
	{ E_BLUNDERSCAN,	"blunderscan",    blunderscan_engine_init,    0 },
#endif
#endif
	{ E_PATTERN,		"pattern",        pattern_engine_init,        1 },
#ifdef EXTRA_ENGINES
	{ E_PATTERNSCAN,	"patternscan",    patternscan_engine_init,    0 },
#endif
	{ E_JOSEKI,		"joseki",         joseki_engine_init,         1 },
	{ E_JOSEKILOAD,		"josekiload",     josekiload_engine_init,     0 },
#ifdef JOSEKIFIX
	{ E_JOSEKIFIX,		"josekifix",	  josekifix_engine_init,      0 },
	{ E_JOSEKIFIXLOAD,	"josekifixload",  josekifixload_engine_init,  0 },
#ifdef EXTRA_ENGINES
	{ E_JOSEKIFIXSCAN,	"josekifixscan",  josekifixscan_engine_init,  0 },
#endif
#endif	
	{ E_RANDOM,		"random",         random_engine_init,         1 },
	{ E_REPLAY,		"replay",         replay_engine_init,         1 },
	{ E_MONTECARLO,		"montecarlo",     montecarlo_engine_init,     1 },
#ifdef DISTRIBUTED
	{ E_DISTRIBUTED,	"distributed",    distributed_engine_init,    1 },
#endif
#ifdef JOSEKIFIX
	{ E_EXTERNAL,		"external",       external_engine_init,       0 },
#endif

/* Alternative names */
	{ E_PATTERN,		"patternplay",    pattern_engine_init,        1 },  /* backwards compatibility */
	{ E_JOSEKI,		"josekiplay",     joseki_engine_init,         1 },
	
	{ 0, 0, 0, 0 }
};

void
engine_init_checks(void)
{
	/* Check engines list is sane. */
	for (int i = 0; i < E_MAX; i++)
		assert(engines[i].name && engines[i].id == i);
}

enum engine_id
engine_name_to_id(const char *name)
{
	for (int i = 0; engines[i].name; i++)
		if (!strcmp(name, engines[i].name))
			return engines[i].id;
	return E_MAX;
}

char*
supported_engines(bool show_all)
{
	static_strbuf(buf, 512);
	for (int i = 0; i < E_MAX; i++)  /* Don't list alt names */
		if (show_all || engines[i].show)
			strbuf_printf(buf, "%s%s", engines[i].name, (engines[i+1].name ? ", " : ""));
	return buf->str;
}


/**************************************************************************************************/
/* Engine options */

/* Parse comma separated @arg, fill @options. */
static void
engine_options_parse(const char *arg, options_t *options)
{
	options->n = 0;
	if (!arg)  return;

	option_t *option = &options->o[0];
	char *tmp_arg = strdup(arg);
	char *next = tmp_arg;
	
	for (options->n = 0;  *next;  options->n++, option++) {
		assert(options->n < ENGINE_OPTIONS_MAX);
		
		char *optspec = next;
		next += strcspn(next, ",");
		if (*next)  *next++ = 0;  else  *next = 0;
		
		char *optname = optspec;
		char *optval = strchr(optspec, '=');
		if (optval)  *optval++ = 0;

		option->name = strdup(optname);
		option->val = (optval ? strdup(optval) : NULL);
	}

	free(tmp_arg);
}

static void
engine_options_free(options_t *options)
{
	for (int i = 0; i < options->n; i++) {
		free(options->o[i].name);
		free(options->o[i].val);
	}
}

/* Add option, overwriting previous value if any. */
void
engine_options_add(options_t *options, const char *name, const char *val)
{
	/* Overwrite existing option ? */
	for (int i = 0; i < options->n; i++) {
		assert(i < ENGINE_OPTIONS_MAX);
		
		if (!strcmp(options->o[i].name, name)) {
			free(options->o[i].val);
			options->o[i].val = (val ? strdup(val) : NULL);
			return;
		}
	}

	/* Ok, new option. */
	options->o[options->n].name = strdup(name);
	options->o[options->n].val = (val ? strdup(val) : NULL);
	options->n++;
}

static void
engine_options_copy(options_t *dest, options_t *src)
{
	memcpy(dest, src, sizeof(*src));
}

void
engine_options_print(options_t *options)
{
	fprintf(stderr, "engine options:\n");
	for (int i = 0; i < options->n; i++)
		if (options->o[i].val)  fprintf(stderr, "  %s=%s\n", options->o[i].name, options->o[i].val);
		else                    fprintf(stderr, "  %s\n", options->o[i].name);
}

option_t *
engine_options_lookup(options_t *options, const char *name)
{
	for (int i = 0; i < options->n; i++)
		if (!strcmp(options->o[i].name, name))
			return &options->o[i];
	return NULL;
}

void
engine_options_concat(strbuf_t *buf, options_t *options)
{
	option_t *option = &options->o[0];
	
	for (int i = 0;  i < options->n;  i++, option++)
		if (option->val)  sbprintf(buf, "%s%s=%s", (i ? "," : ""), option->name, option->val);
		else              sbprintf(buf, "%s%s",    (i ? "," : ""), option->name);
}


/**************************************************************************************************/
/* Engine init */

/* init from scratch, preserving options. */
static void
engine_init_(engine_t *e, int id, board_t *b)
{
	assert(id >= 0 && id < E_MAX);
	
	options_t options;
	//engine_options_print(&e->options);

	engine_options_copy(&options, &e->options);
	memset(e, 0, sizeof(*e));
	engine_options_copy(&e->options, &options);

	e->id = id;
	engines[id].init(e, b);
}

/* init from scratch */
void
engine_init(engine_t *e, int id, const char *e_arg, board_t *b)
{
	engine_options_parse(e_arg, &e->options);
	engine_init_(e, id, b);
}

void
engine_done(engine_t *e)
{
	if (e->done) e->done(e);
	if (e->data) free(e->data);
	engine_options_free(&e->options);
	memset(e, 0, sizeof(*e));
}

engine_t*
new_engine(int id, const char *e_arg, board_t *b)
{
	engine_t *e = malloc2(engine_t);
	engine_init(e, id, e_arg, b);
	return e;
}

void
delete_engine(engine_t **e)
{
	engine_done(*e);
	free(*e);
	*e = NULL;
}

void
engine_reset(engine_t *e, board_t *b)
{
	/* Engine implements its own reset logic ? Don't pull rug under it. */
	if (e->reset) {
		e->reset(e, b);
		return;
	}

	int engine_id = e->id;
	options_t options;
	
	engine_options_copy(&options, &e->options);  /* Save options. */
	
	e->options.n = 0;
	engine_done(e);
	
	engine_options_copy(&e->options, &options);  /* Restore options. */
	engine_init_(e, engine_id, b);
}

void
engine_board_print(engine_t *e, board_t *b, FILE *f)
{
	(e->board_print ? e->board_print(e, b, f) : board_print(b, f));
}

void
engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color, best_moves_t *best)
{
	assert(is_player_color(color));
	e->best_moves(e, b, ti, color, best);
}

ownermap_t*
engine_ownermap(engine_t *e, board_t *b)
{
	return (e->ownermap ? e->ownermap(e, b) : NULL);
}

static void
print_dead_groups(board_t *b, mq_t *dead)
{
	if (!DEBUGL(1)) return;

	for (int i = 0; i < dead->moves; i++) {
		fprintf(stderr, "  ");
		foreach_in_group(b, dead->move[i]) {
			fprintf(stderr, "%s ", coord2sstr(c));
		} foreach_in_group_end;
		fprintf(stderr, "\n");
	}
}

/* Ask engine for dead stones */
void
engine_dead_groups(engine_t *e, board_t *b, mq_t *q)
{
	mq_init(q);

	/* Tell engine to stop pondering, the game is probably over. */
	if (e->stop)  e->stop(e);
	
	if (e->dead_groups)  e->dead_groups(e, b, q);
	/* else we return empty list - i.e. engine not supporting
	 * this assumes all stones alive at the game end. */

	print_dead_groups(b, q);  /* log output */
}

/* For engines internal use, ensures optval is properly strdup'ed / freed
 * since engine may change it. Must be preserved for next engine reset. */
bool
engine_setoption(engine_t *e, board_t *b, option_t *option, char **err, bool setup, bool *reset)
{
	char *optval = (option->val ? strdup(option->val) : NULL);
	bool r = e->setoption(e, b, option->name, optval, err, setup, reset);
	free(optval);
	return r;
}

bool
engine_setoptions(engine_t *e, board_t *b, const char *arg, char **err)
{
	assert(arg);

	options_t options;
	engine_options_parse(arg, &options);

	/* Reset engine if engine doesn't implement setoption(). */
	bool reset = true;

	if (e->setoption) {
		reset = false;
		/* Don't save options until we know they're all good. */
		for (int i = 0; i < options.n; i++)
			if (!engine_setoption(e, b, &options.o[i], err, false, &reset))
				if (!reset)  return false;   /* Failed, err is error msg */
	}
		
	/* Ok, save. */
	for (int i = 0; i < options.n; i++)
		engine_options_add(&e->options, options.o[i].name, options.o[i].val);	

	/* Engine reset needed ? */
	if (reset)  engine_reset(e, b);

	engine_options_free(&options);
	return true;
}


/**************************************************************************************************/

void
best_moves_init(best_moves_t *best, coord_t c[], float r[], int size)
{
	best->n = 0;
	best->size = size;
	best->c = c;
	best->r = r;
	best->d = NULL;

	memset(c, 0, size * sizeof(*c));
	memset(r, 0, size * sizeof(*r));
}

void
best_moves_init_full(best_moves_t *best, coord_t c[], float r[], void* d[], int size)
{
	best->n = 0;
	best->size = size;
	best->c = c;
	best->r = r;
	best->d = d;

	memset(c, 0, size * sizeof(*c));
	memset(r, 0, size * sizeof(*r));
	memset(d, 0, size * sizeof(*d));
}

/* For engines best_move(): Add move @c with prob @r to best moves */
void
best_moves_add(best_moves_t *best, coord_t c, float r)
{
	int i;
	for (i = 0; i < best->n; i++)
		if (r > best->r[i]) {
			/* Found position, shift following moves */
			for (int j = MIN(best->n, best->size - 1); j > i; j--) {
				best->r[j] = best->r[j - 1];
				best->c[j] = best->c[j - 1];
			}
			break;
		}

	/* Add move if we found a spot or there is room left. */
	if (i < best->size) {
		best->r[i] = r;
		best->c[i] = c;
		best->n = MIN(best->n + 1, best->size);
	}
}

void
best_moves_add_full(best_moves_t *best, coord_t c, float r, void *d)
{
	int i;
	for (i = 0; i < best->n; i++)
		if (r > best->r[i]) {
			/* Found position, shift following moves */
			for (int j = MIN(best->n, best->size - 1); j > i; j--) {
				best->r[j] = best->r[j - 1];
				best->c[j] = best->c[j - 1];
				best->d[j] = best->d[j - 1];
			}
			break;
		}

	/* Add move if we found a spot or there is room left. */
	if (i < best->size) {
		best->r[i] = r;
		best->c[i] = c;
		best->d[i] = d;
		best->n = MIN(best->n + 1, best->size);
	}
}

int
best_moves_print(best_moves_t *best, char *str)
{
	fprintf(stderr, "%s[ ", str);
	for (int i = 0; i < best->n; i++)
		fprintf(stderr, "%-3s ", coord2sstr(best->c[i]));
	for (int i = best->n; i < best->size; i++)
		fprintf(stderr, "%-3s ", "");
	fprintf(stderr, "]\n");
	return strlen(str);
}

