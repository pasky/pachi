#ifndef PACHI_JOSEKIFIX_OVERRIDE_H
#define PACHI_JOSEKIFIX_OVERRIDE_H

/* Simple overrides
 *
 * Allows to match board situation based on spatial pattern around last move to for example,
 * detect certain joseki or fuseki sequences no matter in which corner/board orientation/color
 * they are played.
 *
 * joseki_override.h has extra logic for matching joseki sequences (ladder checks etc).
 *
 * Overrides can either specify next move ("just override this move"), or leave it as "pass"
 * to let an external joseki engine take over the following sequence in this quadrant.
 */

/* Pattern dist used for hashes */
#define JOSEKIFIX_OVERRIDE_DIST 10

/* Overrides are represented by this struct. 
 * matching is based on:
 *   - last move
 *   - spatial pattern (radius 5) around last move (or a given coord near it)
 *
 * Coords are just stored as strings: we really don't care about performance here (few
 * entries, running once at the end of genmove) and makes it easy to initialize override
 * structs in code where special handling / experiment is called for. */
typedef struct {
			/* mandatory fields */	
	char* prev;		/* last move */
	char* next;		/* wanted next move. "pass" = external joseki engine mode (see below) */
	char* name;		/* override name (joseki line, fuseki name ...) */
	hash_t hashes[8];       /* spatial hashes for all 8 rotations */
	
			/* optional fields */
	char* coord_own;	/* match pattern around this location instead of last move. */
	char* coord_other;      /* spatial patterns ignore center stone so we need to convey that (3 possibilities) */
	char* coord_empty;      /* set the one corresponding to board position (own color / other color / empty) */
} override_t;

/* Low level override matching */
coord_t check_override(struct board *b, override_t *override, int *prot, hash_t lasth, char *title);
coord_t check_overrides(struct board *b, override_t overrides[], hash_t lasth, char *title);
coord_t check_override_rot(struct board *b, override_t *override, int rot, hash_t lasth);

bool sane_override_move(struct board *b, coord_t c, char *name, char *title);


#endif
