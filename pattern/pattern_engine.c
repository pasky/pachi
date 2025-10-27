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

#define PREDICT_MOVE_MAX 320

/* Internal engine state. */
typedef struct {
	int debug_level;
	bool mcowner_fast;
	
	pattern_config_t pc;
	pattern_t patterns[BOARD_MAX_MOVES];
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
			 pattern_context_t *ct, best_moves_t *best)
{
	fprintf(stderr, "\n");
	for (int i = 0; i < best->n; i++) {
		move_t m = move(best->c[i], color);
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

	pattern_t *pats = pp->patterns;
	floating_t probs[b->flen];
	pattern_context_t *ct = pattern_context_new2(b, color, &pp->pc, pp->mcowner_fast);
	pattern_rate_moves(b, color, probs, pats, ct, &pp->matched_locally);

	float best_r[20];
	coord_t best_c[20];
	best_moves_setup(best, best_c, best_r, 20);
	
	get_pattern_best_moves(b, probs, &best);
	print_pattern_best_moves(&best);

	if (pp->debug_level >= 4)
		debug_pattern_best_moves(pp, b, color, ct, &best);
	if (pp->debug_level >= 5)
		debug_pattern_all_moves(pp, b, probs, pats);

	pattern_context_free(ct);
	return (best.n ? best_c[0] : pass);
}

static void
pattern_engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
			  best_moves_t *best)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;

	pattern_t *pats = pp->patterns;	
	floating_t probs[b->flen];
	pattern_context_t *ct = pattern_context_new2(b, color, &pp->pc, pp->mcowner_fast);
	pattern_rate_moves(b, color, probs, pats, ct, &pp->matched_locally);

	get_pattern_best_moves(b, probs, best);
	print_pattern_best_moves(best);

	pattern_context_free(ct);
}

void
pattern_engine_evaluate(engine_t *e, board_t *b, time_info_t *ti, floating_t *probs, enum stone color)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;

	pattern_t *pats = pp->patterns;
	pattern_context_t *ct = pattern_context_new2(b, color, &pp->pc, pp->mcowner_fast);
	pattern_rate_moves(b, color, probs, pats, ct, &pp->matched_locally);

	if (pp->debug_level >= 4)
		debug_pattern_all_moves(pp, b, probs, pats);

	pattern_context_free(ct);
}

/*************************************************************************************************/
/* t-predict stats */

typedef struct {
	int hits;
	int moves;
} spatial_hits_stats_t;

typedef struct {
	int dist;
	int moves;
} spatial_dist_stats_t;

typedef struct {
	spatial_hits_stats_t	spatial_hits_by_move_number[PREDICT_MOVE_MAX/10];
	spatial_dist_stats_t	spatial_dist_by_move_number[PREDICT_MOVE_MAX/10];
} pattern_stats_t;

static pattern_stats_t pattern_predict_stats;


void pattern_engine_collect_stats(engine_t *e, board_t *b, move_t *m, best_moves_t *best, int moves, int games)
{
	pattern_engine_t *pp = (pattern_engine_t*)e->data;
	pattern_stats_t *stats = &pattern_predict_stats;
	
	coord_t best_move = (best->n ? best->c[0] : pass);
	if (is_pass(best_move))  return;
	
	int best_f = b->fmap[best_move];
	assert(b->f[best_f] == best_move);
	pattern_t *best_pat = &pp->patterns[best_f];

	/* Spatial hits by move number (best move) */
	{
		int i = MIN(b->moves/10, PREDICT_MOVE_MAX/10 - 1);
		if (pattern_biggest_spatial(best_pat))
			stats->spatial_hits_by_move_number[i].hits++;
		stats->spatial_hits_by_move_number[i].moves++;
	}
	
	/* Average spatial dist by move number (best move) */
	if (pattern_biggest_spatial(best_pat))
	{
		int i = MIN(b->moves/10, PREDICT_MOVE_MAX/10 - 1);
		stats->spatial_dist_by_move_number[i].dist += pattern_biggest_spatial(best_pat);
		stats->spatial_dist_by_move_number[i].moves++;
	}
}

static char *stars = "****************************************************************************************************";

void pattern_engine_print_stats(engine_t *e, strbuf_t *buf, int moves, int games)
{
	pattern_stats_t *stats = &pattern_predict_stats;

	sbprintf(buf, "Average spatial dist by move number (best move):\n");
	for (int i = 0; i < PREDICT_MOVE_MAX/10; i++) {
		int dist = stats->spatial_dist_by_move_number[i].dist;
		int moves = stats->spatial_dist_by_move_number[i].moves;
		float avg_dist = (moves ? (float)dist / moves : NAN);
		
		int hits = stats->spatial_hits_by_move_number[i].hits;
		moves = stats->spatial_hits_by_move_number[i].moves;
		int pc = (moves ? round(hits * 100 / moves) : 0);
		
		if (isnan(avg_dist))
			sbprintf(buf, "  move %3i-%-3i:  NA\n", i*10, (i+1)*10-1);
		else
			sbprintf(buf, "  move %3i-%-3i: %4.1f %-22.*s		spatial ?  %4i/%-4i (%3i%%) %.*s\n",
				 i*10, (i+1)*10-1,
				 avg_dist, (int)round(avg_dist * 2), stars,
				 hits, moves, pc, pc/4, stars);
	}
	sbprintf(buf, " \n");
}


/*************************************************************************************************/

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
	e->collect_stats = pattern_engine_collect_stats;
	e->print_stats = pattern_engine_print_stats;
	pattern_engine_state_init(e, b);
}
