#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "pattern/pattern_engine.h"
#include "pattern/pattern.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"
#include "random.h"


/* Internal engine state. */
typedef struct {
	int debug_level;
	pattern_config_t pc;
	bool mcowner_fast;
	bool matched_locally;
} pattern_engine_t;

pattern_config_t*
pattern_engine_get_pc(engine_t *e)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;
	return &pp->pc;
}

bool
pattern_engine_matched_locally(engine_t *e)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;
	return pp->matched_locally;
}

/* Print patterns for best moves */
static void
debug_pattern_best_moves(pattern_engine_t *pp, board_t *b, enum stone color,
			 pattern_context_t *ct, coord_t *best_c, int nbest)
{
	fprintf(stderr, "\n");
	for (int i = 0; i < nbest; i++) {
		move_t m = move(best_c[i], color);
		pattern_t p;
		pattern_match(b, &m, &p, ct, pp->matched_locally);

		strbuf(buf, 512);
		dump_gammas(buf, &p);
		fprintf(stderr, "%3s gamma %s\n", coord2sstr(m.coord), buf->str);
	}
	fprintf(stderr, "\n");
}

/* Print patterns for all moves */
static void
debug_pattern_all_moves(pattern_engine_t *pp, board_t *b, floating_t *probs, pattern_t *pats)
{
	for (int f = 0; f < b->flen; f++) {
		if (probs[f] >= 0.001) {
			char s[256]; pattern2str(s, &pats[f]);
			fprintf(stderr, "\t%s: %.3f %s\n", coord2sstr(b->f[f]), probs[f], s);
		}
	}
}

static coord_t
pattern_engine_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;

	pattern_t pats[b->flen];
	floating_t probs[b->flen];
	pattern_context_t *ct = pattern_context_new2(b, color, &pp->pc, pp->mcowner_fast);
	pattern_rate_moves(b, color, probs, pats, ct, &pp->matched_locally);

	float best_r[20];
	coord_t best_c[20];
	get_pattern_best_moves(b, probs, best_c, best_r, 20);
	print_pattern_best_moves(b, best_c, best_r, 20);

	if (pp->debug_level >= 4)
		debug_pattern_best_moves(pp, b, color, ct, best_c, 20);
	if (pp->debug_level >= 5)
		debug_pattern_all_moves(pp, b, probs, pats);

	pattern_context_free(ct);
	return best_c[0];
}

static void
pattern_engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
			  coord_t *best_c, float *best_r, int nbest)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;

	floating_t probs[b->flen];
	pattern_context_t *ct = pattern_context_new2(b, color, &pp->pc, pp->mcowner_fast);
	pattern_rate_moves(b, color, probs, NULL, ct, &pp->matched_locally);

	get_pattern_best_moves(b, probs, best_c, best_r, nbest);
	print_pattern_best_moves(b, best_c, best_r, nbest);

	pattern_context_free(ct);
}

void
pattern_engine_evaluate(engine_t *e, board_t *b, time_info_t *ti, floating_t *probs, enum stone color)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;

	pattern_t pats[b->flen];
	pattern_context_t *ct = pattern_context_new2(b, color, &pp->pc, pp->mcowner_fast);
	pattern_rate_moves(b, color, probs, pats, ct, &pp->matched_locally);

	if (pp->debug_level >= 4)
		debug_pattern_all_moves(pp, b, probs, pats);

	pattern_context_free(ct);
}

#define NEED_RESET   ENGINE_SETOPTION_NEED_RESET
#define option_error engine_setoption_error

static bool
pattern_engine_setoption(engine_t *e, board_t *b, const char *optname, char *optval,
			 char **err, bool setup, bool *reset)
{
	static_strbuf(ebuf, 256);
	pattern_engine_t *pp = (pattern_engine_t*)e->data;

	if (!strcasecmp(optname, "debug")) {
		if (optval)  pp->debug_level = atoi(optval);
		else         pp->debug_level++;
	}
	else if (!strcasecmp(optname, "mcowner_fast") && optval) {
		/* Use mcowner_fast=0 for better ownermap accuracy,
		 * Will be much slower though. (Default: mcowner_fast=1) 
		 * See also MM_MINGAMES. */
		pp->mcowner_fast = atoi(optval);
	}
	else if (!strcasecmp(optname, "patterns") && optval) {  NEED_RESET
		patterns_init(&pp->pc, optval, false, true);
	}
	else
		option_error("pattern: Invalid engine argument %s or missing value\n", optname);

	return true;
}

pattern_engine_t *
pattern_engine_state_init(engine_t *e, board_t *b)
{
	options_t *options = &e->options;
	pattern_engine_t *pp = calloc2(1, pattern_engine_t);
	e->data = pp;
	
	bool pat_setup = false;

	pp->debug_level = debug_level;
	pp->matched_locally = false;
	pp->mcowner_fast = true;

	/* Process engine options. */
	for (int i = 0; i < options->n; i++) {
		char *err;
		if (!engine_setoption(e, b, &options->o[i], &err, true, NULL))
			die("%s", err);
		if (!strcmp(options->o[i].name, "patterns"))
			pat_setup = true;
	}

	if (!pat_setup)
		patterns_init(&pp->pc, NULL, false, true);
	
	if (!using_patterns())
		die("Missing spatial dictionary / probtable, aborting.\n");
	return pp;
}

void
pattern_engine_init(engine_t *e, board_t *b)
{
	e->name = "Pattern";
	e->comment = "I select moves blindly according to learned patterns. I won't pass as long as there is a place on the board where I can play. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = pattern_engine_genmove;
	e->setoption = pattern_engine_setoption;
	e->best_moves = pattern_engine_best_moves;
	e->evaluate = pattern_engine_evaluate;
	pattern_engine_state_init(e, b);
}
