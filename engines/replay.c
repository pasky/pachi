#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"
#include "../joseki.h"
#include "playout/light.h"
#include "playout/moggy.h"
#include "engines/replay.h"

/* Internal engine state. */
typedef struct {
	int debug_level;
	int runs;
	int no_suicide;
	playout_policy_t *playout;
} replay_t;

static void
suicide_stats(int suicide)
{
	static int total = 0;
	static int suicides = 0;
	if (suicide) suicides++;
	if (++total % 100 == 0)
		fprintf(stderr, "Suicides: %i/%i (%i%%)\n", suicides, total, suicides * 100 / total);
}

coord_t
replay_sample_moves(engine_t *e, board_t *b, enum stone color, 
		    int *played, int *pmost_played)
{
	replay_t *r = e->data;
	playout_policy_t *policy = r->playout;
	playout_setup_t setup;	        memset(&setup, 0, sizeof(setup));
	move_t m = move(pass, color);
	int most_played = 0;
	
	/* Find out what moves policy plays most in this situation */
        for (int i = 0; i < r->runs; i++) {
		board_t b2;
		board_copy(&b2, b);
		
		if (policy->setboard)
			policy->setboard(policy, &b2);
		
		if (DEBUGL(4))  fprintf(stderr, "---------------------------------\n");		
		coord_t c = playout_play_move(&setup, &b2, color, r->playout);		
		assert(!is_resign(c));
		if (DEBUGL(4))  fprintf(stderr, "-> %s\n", coord2sstr(c));
		
		played[c]++;
		if (played[c] > most_played) {
			most_played++;  m.coord = c;
		}

		board_done_noalloc(&b2);
	}
	
	*pmost_played = most_played;
	return m.coord;
}

static coord_t
replay_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	replay_t *r = e->data;
	move_t m = move(pass, color);
	
	if (DEBUGL(3))
		printf("genmove: %s to play. Sampling moves (%i runs)\n", stone2str(color), r->runs);

	int played_[b->size2 + 1];		memset(played_, 0, sizeof(played_));
	int *played = played_ + 1;		// allow storing pass
	int most_played = 0;
	m.coord = replay_sample_moves(e, b, color, played, &most_played);

	if (DEBUGL(3)) {  /* Show moves stats */
		for (int k = most_played; k > 0; k--)
			for (coord_t c = pass; c < b->size2; c++)
				if (played[c] == k)
					fprintf(stderr, "%3s: %.2f%%\n", coord2sstr(c), (float)k * 100 / r->runs);
		fprintf(stderr, "\n");
	}

	if (DEBUGL(2))
		fprintf(stderr, "genmove: %s %s    %.2f%%  (%i runs)\n\n",
			(color == S_BLACK ? "B" : "W"),
			coord2sstr(m.coord), (float)most_played * 100 / r->runs, r->runs);
	
	if (r->no_suicide) {  /* Check group suicides */
		board_t b2;  board_copy(&b2, b);
		int res = board_play(&b2, &m);  assert(res >= 0);
		int suicide = !group_at(&b2, m.coord);
		board_done_noalloc(&b2);
		
		suicide_stats(suicide);		
		if (suicide) {
			if (DEBUGL(2))
				fprintf(stderr, "EEEK, group suicide, will pass instead !\n");
			/* XXX: We should check for non-suicide alternatives. */
			return pass;
		}
	}        

	return m.coord;
}

static void
replay_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
		  coord_t *best_c, float *best_r, int nbest)
{
	replay_t *r = e->data;
	
	if (DEBUGL(3))
		printf("best_moves: %s to play. Sampling moves (%i runs)\n", stone2str(color), r->runs);

	int played_[b->size2 + 1];		memset(played_, 0, sizeof(played_));
	int *played = played_ + 1;		// allow storing pass
	int most_played = 0;
	replay_sample_moves(e, b, color, played, &most_played);
	
	for (coord_t c = pass; c < b->size2; c++)
		best_moves_add(c, (float)played[c] / r->runs, best_c, best_r, nbest);
}

static void
replay_done(engine_t *e)
{
	replay_t *r = e->data;
	playout_policy_done(r->playout);
}

replay_t *
replay_state_init(char *arg, board_t *b)
{
	replay_t *r = calloc2(1, replay_t);
	
	r->debug_level = 1;
	r->runs = 1000;
	r->no_suicide = 0;
	joseki_load(b->size);
	
	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					r->debug_level = atoi(optval);
				else
					r->debug_level++;
			} else if (!strcasecmp(optname, "runs") && optval) {
				/* runs=n  set number of playout runs to sample.
				 *         use runs=1 for raw playout policy */
				r->runs = atoi(optval);
			} else if (!strcasecmp(optname, "no_suicide")) {
				/* ensure engine doesn't allow group suicides
				 * (off by default) */
				r->no_suicide = 1;
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "moggy")) {
					r->playout = playout_moggy_init(playoutarg, b);
				} else if (!strcasecmp(optval, "light")) {
					r->playout = playout_light_init(playoutarg, b);
				} else {
					fprintf(stderr, "Replay: Invalid playout policy %s\n", optval);
				}
			} else
				die("Replay: Invalid engine argument %s or missing value\n", optname);
		}
	}

	if (!r->playout)
		r->playout = playout_moggy_init(NULL, b);
	r->playout->debug_level = r->debug_level;

	return r;
}


void
engine_replay_init(engine_t *e, char *arg, board_t *b)
{
	replay_t *r = replay_state_init(arg, b);
        /* TODO engine_done(), free policy */
	
	e->name = "PlayoutReplay";
	e->comment = "I select the most probable move from moggy playout policy";
	e->genmove = replay_genmove;
	e->best_moves = replay_best_moves;
	e->done = replay_done;
	e->data = r;
}
