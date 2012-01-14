#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "joseki/base.h"
#include "move.h"
#include "playout.h"
#include "playout/light.h"
#include "playout/moggy.h"
#include "replay/replay.h"


/* Internal engine state. */
struct replay {
	int debug_level;
	struct playout_policy *playout;
};


static coord_t *
replay_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	struct replay *r = e->data;
	struct playout_setup s; memset(&s, 0, sizeof(s));

	coord_t coord = r->playout->choose(r->playout, &s, b, color);

	if (!is_pass(coord)) {
		struct move m;
		m.coord = coord; m.color = color;
		if (board_play(b, &m) >= 0)
			goto have_move;

		if (DEBUGL(2)) {
			fprintf(stderr, "Pre-picked move %d,%d is ILLEGAL:\n",
				coord_x(coord, b), coord_y(coord, b));
			board_print(b, stderr);
		}
	}

	/* Defer to uniformly random move choice. */
	board_play_random(b, color, &coord, (ppr_permit) r->playout->permit, r->playout);

have_move:
	if (!group_at(b, coord)) {
		/* This was suicide. Just pass. */
		/* XXX: We should check for non-suicide alternatives. */
		return coord_pass();
	}

	return coord_copy(coord);
}


struct replay *
replay_state_init(char *arg, struct board *b)
{
	struct replay *r = calloc2(1, sizeof(struct replay));

	r->debug_level = 1;

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
			} else if (!strcasecmp(optname, "playout") && optval) {
				char *playoutarg = strchr(optval, ':');
				if (playoutarg)
					*playoutarg++ = 0;
				if (!strcasecmp(optval, "moggy")) {
					r->playout = playout_moggy_init(playoutarg, b, joseki_load(b->size));
				} else if (!strcasecmp(optval, "light")) {
					r->playout = playout_light_init(playoutarg, b);
				} else {
					fprintf(stderr, "Replay: Invalid playout policy %s\n", optval);
				}
			} else {
				fprintf(stderr, "Replay: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	if (!r->playout)
		r->playout = playout_light_init(NULL, b);
	r->playout->debug_level = r->debug_level;

	return r;
}

struct engine *
engine_replay_init(char *arg, struct board *b)
{
	struct replay *r = replay_state_init(arg, b);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "PlayoutReplay";
	e->comment = "I select moves blindly according to playout policy. I won't pass as long as there is a place on the board where I can play. When we both pass, I will consider all the stones on the board alive.";
	e->genmove = replay_genmove;
	e->data = r;

	return e;
}
