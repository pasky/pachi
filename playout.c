//#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "ownermap.h"
#include "tactics/bent4.h"
#include "playout.h"

void
amaf_init(amafmap_t *amaf)
{
	amaf->gamelen = amaf->game_baselen = 0;
}

/* Full permit logic, ie m->coord may get changed to an alternative move */
static bool
playout_permit_move(playout_policy_t *p, board_t *b, move_t *m, bool alt, bool rnd)
{
	coord_t coord = m->coord;
	if (coord == pass) return false;

#ifdef EXTRA_CHECKS
	assert(is_player_color(m->color));
	assert(sane_coord(coord));
#endif

	bool permit =  (board_permit(b, m, NULL) &&
			(!p->permit || p->permit(p, b, m, alt, rnd)));

	if (DEBUGL(5)) {
		if (!permit)
			fprintf(stderr, "Playout permit(%s): rejected\n", coord2sstr(coord));
		if (permit && m->coord != coord)
			fprintf(stderr, "Playout permit(%s): redirect -> %s\n", coord2sstr(coord), coord2sstr(m->coord));
	}

	return permit;
}

/* Return coord if move is ok, an alternative move or pass if not.
 * Not to be used with randomly picked moves (calls permit_move() with rnd=false). */
static coord_t
playout_check_move(playout_policy_t *p, board_t *b, coord_t coord, enum stone color)
{
	move_t m = move(coord, color);
	if (!playout_permit_move(p, b, &m, true, false))
		return pass;
	return m.coord;
}

/* Is *this* move permitted ? 
 * Called by policy permit() to check something so never the main permit() call. */
bool
playout_permit(playout_policy_t *p, board_t *b, coord_t coord, enum stone color, bool rnd)
{
	move_t m = move(coord, color);
	return playout_permit_move(p, b, &m, false, rnd);
}

static bool
random_permit_handler(board_t *b, move_t *m, void *data)
{
	playout_policy_t *policy = (playout_policy_t*)data;

	//if (DEBUGL(5)) fprintf(stderr, "Trying %s\n", coord2sstr(m->coord));
	return playout_permit_move(policy, b, m, true, true);
}


coord_t
playout_play_move(playout_t *playout, board_t *b, enum stone color)
{
	playout_setup_t *setup = playout->setup;
	playout_policy_t *policy = playout->policy;
	coord_t coord, playout_coord;

	playout_coord = policy->choose(policy, setup, b, color);
	if (DEBUGL(5))  fprintf(stderr, "Playout move: %s\n", coord2sstr(playout_coord));
	coord = playout_check_move(policy, b, playout_coord, color);

	/* Show if playout move is rejected. */
	if (DEBUGL(5) && coord != playout_coord)
		fprintf(stderr, "Playout move %s was invalid !\n", coord2sstr(playout_coord));

	if (!is_pass(coord)) {
		move_t m = move(coord, color);
		int r = board_play(b, &m);
		if (unlikely(r < 0)) {
			board_print(b, stderr);
			die("Picked playout move %s %s is ILLEGAL:\n", stone2str(color), coord2sstr(coord));
		}
		return coord;
	}

	/* Defer to uniformly random move choice if policy failed to produce a move.
	 * This must never happen if the policy is tracking internal board state, obviously. */
	if (DEBUGL(5))  fprintf(stderr, "Playout random move:\n");
	assert(!policy->setboard || policy->setboard_randomok);
	coord = board_play_random(b, color, random_permit_handler, policy);
	if (DEBUGL(5))  fprintf(stderr, "Playout random move: %s\n", coord2sstr(coord));
	return coord;
}


#define random_game_loop_stuff  \
		if (DEBUGL(5)) board_print(b, stderr); \
\
		if (unlikely(is_pass(coord)))  passes++; \
		else                           passes = 0; \
\
		if (amafmap) { \
			assert(amafmap->gamelen < MAX_GAMELEN); \
			amafmap->is_ko_capture[amafmap->gamelen] = board_playing_ko_threat(b); \
			amafmap->game[amafmap->gamelen++] = coord; \
		} \
\
		if (setup->mercymin && abs(b->captures[S_BLACK] - b->captures[S_WHITE]) > setup->mercymin) \
			break; \
\
		color = stone_other(color);


floating_t
playout_play_game(playout_t *playout, board_t *b, enum stone starting_color,
		  amafmap_t *amafmap, ownermap_t *ownermap)
{
	if (DEBUGL(5))  fprintf(stderr, "------------------------------- playout start -------------------------------\n\n");

	assert(playout && playout->setup && playout->policy);
	playout_setup_t *setup = playout->setup;
	playout_policy_t *policy = playout->policy;
	
	b->playout_board = true;   // don't need board hash, history ...

	int starting_passes[S_MAX];
	memcpy(starting_passes, b->passes, sizeof(starting_passes));

	int gamelen = setup->gamelen - b->moves;

	if (policy->setboard)
		policy->setboard(policy, b);

	enum stone color = starting_color;
	int passes = is_pass(last_move(b).coord) && b->moves > 0;

	/* Play until both sides pass, or we hit threshold. */
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord = playout_play_move(playout, b, color);
		random_game_loop_stuff
	}

	if (DEBUGL(5))  fprintf(stderr, "------------------------------- bent4 handling -------------------------------\n\n");

	/* Play some more, handling bent-fours this time ... */
	bent4_t b4;  bent4_init(&b4, b);
	passes = 0;
	while (gamelen-- > 0 && passes < 2) {
		coord_t coord = bent4_play_move(&b4, playout, b, color);
		random_game_loop_stuff
	}
	
	/* Territory scoring: score starting board, using playouts as confirmation phase.
	 * Like in a real game where players disagree about life and death:
	 * They play it out and rewind state for scoring once agreement is reached.
	 * Trying to score final boards directly is too noisy, random passes change the score...
	 * TODO: handle eyes in seki according to japanese rules. */
	if (b->rules == RULES_JAPANESE) {
		memcpy(b->passes, starting_passes, sizeof(starting_passes));
		last_move(b).color = stone_other(starting_color);
	}

	floating_t score = board_fast_score(b);

	if (DEBUGL(5)) {
		fprintf(stderr, "Playout finished: score: %s+%.1f\n\n", (score > 0 ? "W" : "B"), fabs(score));
		fprintf(stderr, "------------------------------- playout end -------------------------------\n\n");
	}

	if (ownermap)  ownermap_fill(ownermap, b, score);

	return score;
}


void
playout_policy_done(playout_policy_t *p)
{
	if (p->done) p->done(p);
	if (p->data) free(p->data);
	free(p);
}


