#define DEBUG
#include "engine.h"
#include "pachi.h"


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


/**************************************************************************************************/

/* init from scratch, preserving options. */
static void
engine_init_(engine_t *e, int id, board_t *b)
{
	options_t options;
	//engine_options_print(&e->options);

	engine_options_copy(&options, &e->options);
	memset(e, 0, sizeof(*e));
	engine_options_copy(&e->options, &options);

	e->id = id;
	pachi_engine_init(e, id, b);
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
	int engine_id = e->id;
	options_t options;
	
	engine_options_copy(&options, &e->options);  /* Save options. */
	
	e->options.n = 0;
	b->es = NULL;
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
engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color, 
		  coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}
	e->best_moves(e, b, ti, color, best_c, best_r, nbest);
}

ownermap_t*
engine_ownermap(engine_t *e, board_t *b)
{
	return (e->ownermap ? e->ownermap(e, b) : NULL);
}


/**************************************************************************************************/

/* For engines best_move(): Add move @c with prob @r to best moves @best_c, @best_r */
void
best_moves_add(coord_t c, float r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++)
		if (r > best_r[i]) {
			for (int j = nbest - 1; j > i; j--) { // shift
				best_r[j] = best_r[j - 1];
				best_c[j] = best_c[j - 1];
			}
			best_r[i] = r;
			best_c[i] = c;
			break;
		}
}

void
best_moves_add_full(coord_t c, float r, void *d, coord_t *best_c, float *best_r, void **best_d, int nbest)
{
	for (int i = 0; i < nbest; i++)
		if (r > best_r[i]) {
			for (int j = nbest - 1; j > i; j--) { // shift
				best_r[j] = best_r[j - 1];
				best_c[j] = best_c[j - 1];
				best_d[j] = best_d[j - 1];
			}
			best_r[i] = r;
			best_c[i] = c;
			best_d[i] = d;
			break;
		}
}

int
best_moves_print(board_t *b, char *str, coord_t *best_c, int nbest)
{
	fprintf(stderr, "%s[ ", str);
	for (int i = 0; i < nbest; i++) {
		const char *str = (is_pass(best_c[i]) ? "" : coord2sstr(best_c[i]));
		fprintf(stderr, "%-3s ", str);
	}
	fprintf(stderr, "]\n");
	return strlen(str);
}

