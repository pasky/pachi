#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "joseki/joseki.h"
#include "joseki/base.h"


/* Internal engine state. */
struct joseki_engine {
	int debug_level;
	bool discard;

	int size;
	struct joseki_dict *jdict;

	struct board *b[16]; // boards with reversed color, mirrored and rotated
};

/* We will record the joseki positions into incrementally-built
 * jdict->patterns[]. */


static char *
joseki_play(struct engine *e, struct board *b, struct move *m, char *enginearg)
{
	struct joseki_engine *j = e->data;

	if (!b->moves) {
		/* New game, reset state. */
		j->size = board_size(b);
		if (j->jdict)
			assert(j->size == j->jdict->bsize);
		else
			j->jdict = joseki_init(j->size);
		j->discard = false;
		for (int i = 0; i < 16; i++) {
			board_resize(j->b[i], j->size - 2);
			board_clear(j->b[i]);
		}
	}

	//printf("%s %d\n", coord2sstr(m->coord, b), coord_quadrant(m->coord, b));

	assert(!is_resign(m->coord));
	if (is_pass(m->coord))
		return NULL;
	/* Ignore moves in different quadrants. */
	if (coord_quadrant(m->coord, b) > 0)
		return NULL;

	if (coord_x(m->coord, b) == board_size(b) / 2 || coord_y(m->coord, b) == board_size(b) / 2) {
		/* This is troublesome, since it cannot mirror properly:
		 * it won't be hashed in some quadrants. Better just discard
		 * the rest of the sequence for now. (TODO: Make quadrants
		 * overlap.) */
		j->discard = true;
	}
	if (j->discard)
		return NULL;

	//printf("%"PRIhash" %"PRIhash"\n", j->b[0]->qhash[0], b->qhash[0]);
	assert(j->b[0]->qhash[0] == b->qhash[0]);

	/* Record next move in all rotations and update the hash. */
	for (int i = 0; i < 16; i++) {
#define HASH_VMIRROR     1
#define HASH_HMIRROR     2
#define HASH_XYFLIP      4
#define HASH_OCOLOR      8
		int quadrant = 0;
		coord_t coord = m->coord;
		if (i & HASH_VMIRROR) {
			coord = coord_xy(b, coord_x(coord, b), board_size(b) - 1 - coord_y(coord, b));
			quadrant += 2;
		}
		if (i & HASH_HMIRROR) {
			coord = coord_xy(b, board_size(b) - 1 - coord_x(coord, b), coord_y(coord, b));
			quadrant++;
		}
		if (i & HASH_XYFLIP) {
			coord = coord_xy(b, coord_y(coord, b), coord_x(coord, b));
			if (quadrant == 1)
				quadrant = 2;
			else if (quadrant == 2)
				quadrant = 1;
		}
		enum stone color = m->color;
		if (i & HASH_OCOLOR)
			color = stone_other(color);

		coord_t **ccp = &j->jdict->patterns[j->b[i]->qhash[quadrant] & joseki_hash_mask].moves[color - 1];

		int count = 1;
		if (*ccp) {
			for (coord_t *cc = *ccp; !is_pass(*cc); cc++) {
				count++;
				if (*cc == coord) {
					//printf("%d,%d (%"PRIhash", %d) !+ %s\n", i, quadrant, j->b[i]->qhash[quadrant], count, coord2sstr(coord, b));
					goto already_have;
				}
			}
		}

		//printf("%d,%d (%"PRIhash", %d) =+ %s\n", i, quadrant, j->b[i]->qhash[quadrant], count, coord2sstr(coord, b));
		*ccp = realloc(*ccp, (count + 1) * sizeof(coord_t));
		(*ccp)[count - 1] = coord;
		(*ccp)[count] = pass;

already_have: {
		struct move m2 = { .coord = coord, .color = color };
		board_play(j->b[i], &m2);
	      }
	}

	return NULL;
}

static coord_t *
joseki_genmove(struct engine *e, struct board *b, struct time_info *ti, enum stone color, bool pass_all_alive)
{
	fprintf(stderr, "genmove command not available in joseki scan!\n");
	exit(EXIT_FAILURE);
}

void
engine_joseki_done(struct engine *e)
{
	struct joseki_engine *j = e->data;
	struct board *b = board_init(NULL);
	board_resize(b, j->size - 2);
	board_clear(b);

	for (hash_t i = 0; i < 1 << joseki_hash_bits; i++) {
		for (int s = 0; s < 2; s++) {
			static const char cs[] = "bw";
			if (!j->jdict->patterns[i].moves[s])
				continue;
			printf("%" PRIhash " %c", i, cs[s]);
			coord_t *cc = j->jdict->patterns[i].moves[s];
			int count = 0;
			while (!is_pass(*cc)) {
				printf(" %s", coord2sstr(*cc, b));
				cc++, count++;
			}
			printf(" %d\n", count);
		}
	}

	board_done(b);

	joseki_done(j->jdict);
}


struct joseki_engine *
joseki_state_init(char *arg)
{
	struct joseki_engine *j = calloc2(1, sizeof(struct joseki_engine));

	for (int i = 0; i < 16; i++)
		j->b[i] = board_init(NULL);

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

			} else {
				fprintf(stderr, "joseki: Invalid engine argument %s or missing value\n", optname);
				exit(EXIT_FAILURE);
			}
		}
	}

	return j;
}

struct engine *
engine_joseki_init(char *arg, struct board *b)
{
	struct joseki_engine *j = joseki_state_init(arg);
	struct engine *e = calloc2(1, sizeof(struct engine));
	e->name = "Joseki";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of joseki sequences fed to it within the GTP stream.";
	e->genmove = joseki_genmove;
	e->notify_play = joseki_play;
	e->done = engine_joseki_done;
	e->data = j;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;

	return e;
}
