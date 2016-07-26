#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "playout.h"
#include "joseki/base.h"
#include "playout/light.h"
#include "playout/moggy.h"
#include "replay/replay.h"

/* Internal engine state. */
struct replay {
	int debug_level;
	int runs;
	int no_suicide;
	struct joseki_dict *jdict;
	struct playout_policy *playout;
};

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
replay_sample_moves(struct engine *e, struct board *b, enum stone color, 
		    int *played, int *pmost_played)
{
	struct replay *r = e->data;
	struct playout_policy *policy = r->playout;
	struct playout_setup setup;	        memset(&setup, 0, sizeof(setup));
	struct move m = { .coord = pass, .color = color };
	int most_played = 0;
	
	/* Find out what moves policy plays most in this situation */
        for (int i = 0; i < r->runs; i++) {
		struct board b2;
		board_copy(&b2, b);
		
		if (policy->setboard)
			policy->setboard(policy, &b2);
		
		if (DEBUGL(4))  fprintf(stderr, "---------------------------------\n");		
		coord_t c = play_random_move(&setup, &b2, color, r->playout);		
		if (DEBUGL(4))  fprintf(stderr, "-> %s\n", coord2sstr(c, &b2));
		
		played[c]++;
		if (played[c] > most_played) {
			most_played++;  m.coord = c;
		}

		board_done_noalloc(&b2);
	}
	
	*pmost_played = most_played;
	return m.coord;
}

static coord_t *
replay_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	struct replay *r = e->data;
	struct move m = { .coord = pass, .color = color };
	
        if (DEBUGL(3))
	      printf("genmove: %s to play. Sampling moves (%i runs)\n", stone2str(color), r->runs);

        int played_[b->size2 + 2];		memset(played_, 0, sizeof(played_));
	int *played = played_ + 2;		// allow storing pass/resign
        int most_played = 0;
	m.coord = replay_sample_moves(e, b, color, played, &most_played);

	if (DEBUGL(3)) {  /* Show moves stats */
		for (int k = most_played; k > 0; k--)
			for (coord_t c = resign; c < b->size2; c++)
				if (played[c] == k)
					fprintf(stderr, "%3s: %.2f%%\n", coord2str(c, b), (float)k * 100 / r->runs);
		fprintf(stderr, "\n");
	}

	if (DEBUGL(2))
		fprintf(stderr, "genmove: %s %s    %.2f%%  (%i runs)\n\n",
			(color == S_BLACK ? "B" : "W"),
			coord2str(m.coord, b), (float)most_played * 100 / r->runs, r->runs);
	
	if (r->no_suicide) {  /* Check group suicides */
		struct board b2;  board_copy(&b2, b);
		int res = board_play(&b2, &m);  assert(res >= 0);
		int suicide = !group_at(&b2, m.coord);
		board_done_noalloc(&b2);
		
		suicide_stats(suicide);		
		if (suicide) {
			if (DEBUGL(2))
				fprintf(stderr, "EEEK, group suicide, will pass instead !\n");
			/* XXX: We should check for non-suicide alternatives. */
			return coord_copy(pass);
		}
	}        

	return coord_copy(m.coord);
}

static void
replay_done(struct engine *e)
{
	struct replay *r = e->data;
	playout_policy_done(r->playout);
	joseki_done(r->jdict);
}

struct replay *
replay_state_init(char *arg, struct board *b)
{
	struct replay *r = calloc2(1, sizeof(struct replay));
	
	r->debug_level = 1;
	r->runs = 1000;
	r->no_suicide = 0;
	r->jdict = joseki_load(b->size);
	
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
					r->playout = playout_moggy_init(playoutarg, b, r->jdict);
				} else if (!strcasecmp(optval, "light")) {
					r->playout = playout_light_init(playoutarg, b);
				} else {
					fprintf(stderr, "Replay: Invalid playout policy %s\n", optval);
				}
			} else {
				fprintf(stderr, "Replay: Invalid engine argument %s or missing value\n", optname);
				exit(1);
			}
		}
	}

	if (!r->playout)
		r->playout = playout_moggy_init(NULL, b, r->jdict);
	r->playout->debug_level = r->debug_level;

	return r;
}


struct engine *
engine_replay_init(char *arg, struct board *b)
{
	struct replay *r = replay_state_init(arg, b);
        /* TODO engine_done(), free policy */
	
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "PlayoutReplay";
	e->comment = "I select the most probable move from moggy playout policy";
	e->genmove = replay_genmove;
	e->done = replay_done;
	e->data = r;

	return e;
}
