#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "josekiload.h"
#include "tactics/util.h"
#include "joseki.h"


/* Internal engine state. */
typedef struct {
	int debug_level;
	
	board_t *b[16]; // boards with reversed color, mirrored and rotated
	josekipat_t *prev[16];
	int next_flags;
} josekiload_t;

#define board_captures(b)  (b->captures[S_BLACK] + b->captures[S_WHITE])

/* Record joseki moves into incrementally-built jdict->hash[]. */
static char *
josekiload_play(engine_t *e, board_t *board, move_t *m, char *move_tags, bool *board_print)
{
	josekiload_t *j = (josekiload_t*)e->data;

	if (!board->moves) {
		/* New game, reset state. */
		assert(board_rsize(board) == joseki_dict->bsize);
		
		for (int i = 0; i < 16; i++) {
			board_resize(j->b[i], board_rsize(board));
			board_clear(j->b[i]);
		}

		memset(j->prev, 0, sizeof(j->prev));
		j->next_flags = 0;
	}

	//board_print(board, stderr);
	//fprintf(stderr, "move: %s %s\n", stone2str(m->color), coord2sstr(m->coord));
	
	assert(!is_resign(m->coord));
	/* pass -> tag next move <later> */
	if (is_pass(m->coord)) {  j->next_flags |= JOSEKI_FLAGS_LATER;  return NULL;  }
	
	int flags = j->next_flags;   j->next_flags = 0;

	/* Don't add setup stones to joseki ! */
	bool setup_stones = (strstr(move_tags, "setup") != NULL);

	/* Not joseki, but keep pattern to match follow-up. */
	if (strstr(move_tags, "ignore"))  flags |= JOSEKI_FLAGS_IGNORE;

	/* Match 3x3 pattern only */
	if (strstr(move_tags, "3x3"))     flags |= JOSEKI_FLAGS_3X3;

	/* Play later */
	if (strstr(move_tags, "later"))	  flags |= JOSEKI_FLAGS_LATER;

	assert(joseki_spatial_hash(j->b[0], m->coord, m->color) ==
	       joseki_spatial_hash(board,   m->coord, m->color));

	coord_t last = last_move(board).coord;
	if (last != pass && coord_gridcular_distance(m->coord, last) >= 30)
		fprintf(stderr, "warning: josekiload %s %s: big distance to prev move, use pass / setup stones for tenuki\n",
			coord2sstr(last), coord2sstr(m->coord));

	/* Record next move in all rotations and add joseki pattern. */
	for (int i = 0; i < 16; i++) {
		board_t *b = j->b[i];
		coord_t coord = rotate_coord(m->coord, i);
		enum stone color = m->color;
		if (i & 8) color = stone_other(color);

		/* add new pattern */
		if (setup_stones)  j->prev[i] = NULL;
		else               j->prev[i] = joseki_add(joseki_dict, b, coord, color, j->prev[i], flags);

		int captures = board_captures(b);
		move_t m2 = move(coord, color);
		int r = board_play(b, &m2);  assert(r >= 0);

		/* update prev pattern if stones were captured, board configuration changed ! */
		if (board_captures(b) != captures && !setup_stones)
			j->prev[i] = joseki_add(joseki_dict, b, coord, color, NULL, flags);
	}

	return NULL;
}

static coord_t
josekiload_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	die("genmove command not available in josekiload engine!\n");
}

static josekiload_t *
josekiload_state_init(engine_t *e)
{
	options_t *options = &e->options;
	josekiload_t *j = calloc2(1, josekiload_t);

	for (int i = 0; i < 16; i++)
		j->b[i] = board_new(19, NULL);

	j->debug_level = 1;

	for (int i = 0; i < options->n; i++) {
		const char *optname = options->o[i].name;
		const char *optval = options->o[i].val;

		if (!strcasecmp(optname, "debug")) {
			if (optval)
				j->debug_level = atoi(optval);
			else
				j->debug_level++;

		} else
			die("josekiload: Invalid engine argument %s or missing value\n", optname);
	}

	return j;
}

static void
josekiload_done(engine_t *e)
{
	josekiload_t *j = (josekiload_t*)e->data;

	for (int i = 0; i < 16; i++)
		board_delete(&j->b[i]);
}

void
josekiload_engine_init(engine_t *e, board_t *b)
{
	josekiload_t *j = josekiload_state_init(e);
	e->name = "JosekiLoad";
	e->comment = "You cannot play Pachi with this engine, it is intended for internal use (loading joseki sequences)";
	e->genmove = josekiload_genmove;
	e->notify_play = josekiload_play;
	e->done = josekiload_done;
	e->data = j;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;
}
