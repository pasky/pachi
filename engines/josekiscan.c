#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "engines/josekiscan.h"
#include "tactics/util.h"
#include "../joseki.h"


/* Internal engine state. */
struct josekiscan_engine {
	int debug_level;
	
	struct board *b[16]; // boards with reversed color, mirrored and rotated
	josekipat_t *prev[16];
	int next_flags;
};

#define board_captures(b)  (b->captures[S_BLACK] + b->captures[S_WHITE])

/* Record joseki moves into incrementally-built jdict->hash[]. */
static char *
josekiscan_play(struct engine *e, struct board *board, struct move *m, char *move_tags)
{
	struct josekiscan_engine *j = e->data;

	if (!board->moves) {
		/* New game, reset state. */
		assert(board->size == joseki_dict->bsize);
		
		for (int i = 0; i < 16; i++) {
			board_resize(j->b[i], board->size - 2);
			board_clear(j->b[i]);
		}

		memset(j->prev, 0, sizeof(j->prev));
		j->next_flags = 0;
	}

	//board_print(board, stderr);
	//fprintf(stderr, "move: %s %s\n", stone2str(m->color), coord2sstr(m->coord, board));
	
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

	coord_t last = board->last_move.coord;
	if (last != pass && coord_gridcular_distance(m->coord, last, board) >= 30)
		fprintf(stderr, "warning: josekiscan %s %s: big distance to prev move, use pass / setup stones for tenuki\n",
			coord2sstr(last, board), coord2sstr(m->coord, board));

	/* Record next move in all rotations and add joseki pattern. */
	for (int i = 0; i < 16; i++) {
		struct board *b = j->b[i];
		coord_t coord = rotate_coord(b, m->coord, i);
		enum stone color = m->color;
		if (i & 8) color = stone_other(color);

		/* add new pattern */
		if (setup_stones)  j->prev[i] = NULL;
		else               j->prev[i] = joseki_add(joseki_dict, b, coord, color, j->prev[i], flags);

		int captures = board_captures(b);
		struct move m2 = move(coord, color);
		int r = board_play(b, &m2);  assert(r >= 0);

		/* update prev pattern if stones were captured, board configuration changed ! */
		if (board_captures(b) != captures && !setup_stones)
			j->prev[i] = joseki_add(joseki_dict, b, coord, color, NULL, flags);
	}

	return NULL;
}

static coord_t
josekiscan_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	die("genmove command not available in josekiscan engine!\n");
}

static struct josekiscan_engine *
josekiscan_state_init(char *arg)
{
	struct josekiscan_engine *j = calloc2(1, sizeof(struct josekiscan_engine));

	for (int i = 0; i < 16; i++)
		j->b[i] = board_new(19+2, NULL);

	j->debug_level = 1;

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
					j->debug_level = atoi(optval);
				else
					j->debug_level++;

			} else
				die("josekiscan: Invalid engine argument %s or missing value\n", optname);
		}
	}

	return j;
}

static void
josekiscan_done(struct engine *e)
{
	struct josekiscan_engine *j = e->data;

	for (int i = 0; i < 16; i++)
		board_done(j->b[i]);
}

void
engine_josekiscan_init(struct engine *e, char *arg, struct board *b)
{
	struct josekiscan_engine *j = josekiscan_state_init(arg);
	e->name = "Josekiscan";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of joseki sequences fed to it within the GTP stream.";
	e->genmove = josekiscan_genmove;
	e->notify_play = josekiscan_play;
	e->done = josekiscan_done;
	e->data = j;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;
}
