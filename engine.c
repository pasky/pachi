#define DEBUG
#include <unistd.h>

#include "engine.h"
#include "debug.h"
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

/* Add option, overwriting previous value if any. */
static void
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

/* Ask GnuGo for dead groups.
 * GnuGo is really good at scoring finished games, does better than
 * playouts which get tripped by some sekis in certain situations.
 * Right now Pachi gets about 95% of games right, GnuGo 99%. */
static void
gnugo_dead_groups(gtp_t *gtp, board_t *b, move_queue_t *q)
{
	if (DEBUGL(3)) fprintf(stderr, "using gnugo for dead stones\n");
		
	/* Generate gtp commands for game */
	
	char file_in[1024] = "pachi.XXXXXX";
	int fd = pachi_mkstemp(file_in, sizeof(file_in));	if (fd == -1)  fail("mkstemp");
	FILE *f = fdopen(fd, "w");				if (!f)        fail("fdopen");
	fprintf(f, "boardsize %i\n", board_rsize(b));
	fprintf(f, "clear_board\n");
	fprintf(f, "komi %.1f\n", b->komi);
	/* don't bother with handicap, only care about dead stones */		
	for (int i = 0; i < gtp->moves; i++)
		fprintf(f, "play %c %s\n", stone2str(gtp->move[i].color)[0], coord2str(gtp->move[i].coord));
	fprintf(f, "final_status_list dead\n");
	fclose(f);  f = NULL;  fd = -1;

	char cmd[256];
	if (DEBUGL(4)) {
		fprintf(stderr, "---------------- in -----------------\n");
		snprintf(cmd, sizeof(cmd), "cat %s 1>&2", file_in);
		if (system(cmd) != 0)		warning("system(%s) failed\n", cmd);
		fprintf(stderr, "-------------------------------------\n");
	}
	
	/* And fire up GnuGo on it */
	
	char file_out[1024] = "pachi.XXXXXX";
	fd = pachi_mkstemp(file_out, sizeof(file_out));		if (fd == -1)  fail("mkstemp");
	close(fd);  fd = -1;
	char *rules;
	if      (b->rules == RULES_JAPANESE)  rules = "--japanese-rules";
	else if (b->rules == RULES_CHINESE)   rules = "--chinese-rules";
	else die("rules must be japanese or chinese when scoring with gnugo\n");
	snprintf(cmd, sizeof(cmd), "%s --mode gtp %s < %s > %s", gnugo_exe, rules, file_in, file_out);
	if (DEBUGL(4))  fprintf(stderr, "cmd: '%s'\n", cmd);
	double time_start = time_now();
	if (system(cmd) != 0)  die("couldn't run gnugo\n");
	if (DEBUGL(2)) fprintf(stderr, "gnugo dead stones in %.1fs\n", time_now() - time_start);

	if (DEBUGL(4)) {
		fprintf(stderr, "---------------- out -----------------\n");
		snprintf(cmd, sizeof(cmd), "cat %s 1>&2", file_out);
		if (system(cmd) != 0)		warning("system(%s) failed\n", cmd);
		fprintf(stderr, "-------------------------------------\n");
	}
	
	/* Extract output */
	
	f = fopen(file_out, "r");      if (!f) fail("fopen");
	char buf[256];
	while (fgets(buf, sizeof(buf), f)) {
		char *line = buf;
		if (!strcmp(line, "= \n") || !strcmp(line, "\n"))  continue;
		
		if (line[0] == '?')          die("Eeeek, some gnugo commands failed !\n");
		if (str_prefix("= ", line))  line += 2;  /* first line, eat up prefix */

		/* One group per line, just get first coord. */
		assert(line[0] && isalpha(line[0]));
		coord_t c = str2coord_for(line, board_rsize(b));  assert(c != pass);
		group_t g = group_at(b, c);   assert(g);
		mq_add(q, g, 0);
	}
	fclose(f);  f = NULL;
		
	unlink(file_in);
	unlink(file_out);
}

static void
print_dead_groups(board_t *b, move_queue_t *dead)
{
	if (!DEBUGL(1)) return;

	for (unsigned int i = 0; i < dead->moves; i++) {
		fprintf(stderr, "  ");
		foreach_in_group(b, dead->move[i]) {
			fprintf(stderr, "%s ", coord2sstr(c));
		} foreach_in_group_end;
		fprintf(stderr, "\n");
	}
}

/* Ask engine for dead stones, or use gnugo if --accurate-scoring */
void
engine_dead_groups(engine_t *e, gtp_t *gtp, board_t *b, move_queue_t *q)
{
	mq_init(q);

	/* Tell engine to stop pondering, the game is probably over. */
	if (e->stop)  e->stop(e);
	
	if (gtp->accurate_scoring)
		gnugo_dead_groups(gtp, b, q);
	else
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

