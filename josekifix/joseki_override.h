#ifndef PACHI_JOSEKI_OVERRIDE_H
#define PACHI_JOSEKI_OVERRIDE_H

#ifdef JOSEKIFIX

/* Joseki / Fuseki overrides
 *
 * Allows to override engine moves based on spatial pattern around last move to
 * for example, fix joseki sequences that dcnn plays poorly.
 *
 * Overrides can either specify next move ("just override this move"), or leave it as "pass"
 * to let an external joseki engine take over the following sequence in this quadrant.
 */

#define JOSEKIFIX_LADDER_SETUP_MAX 5


/* Ladder testing */
typedef struct {
	bool  own_color;		/* ladder color  (own color / other color) */
	char* coord;			/* starting point */
	bool  works;			/* wanted result */
        char* setup_own[JOSEKIFIX_LADDER_SETUP_MAX];		/* setup stones */
        char* setup_other[JOSEKIFIX_LADDER_SETUP_MAX];
} ladder_check_t;


/* Overrides are represented by this struct. 
 * matching is based on:
 *   - last move
 *   - spatial pattern (radius 5) around last move (or a given coord near it)
 *   - optionally ladder checks (override specifies ladder setup).
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
	char* coord;			/* match pattern around this location instead of last move. */
	ladder_check_t ladder_check;	/* ladder checks */
	ladder_check_t ladder_check2;

#define DEFAULT_EXTERNAL_ENGINE_MOVES	15
        int external_engine_mode[4];    /* if set, external engine handles follow-up (one value per quadrant).
					 * value specifies number of external engine moves to play.
					 * note: can also just set "pass" as next move instead of filling this
					 *       (= enable for current quadrant, 15 moves)  */
	int priority;			/* pattern priority  (in case there are multiple matches) */	
} joseki_override_t;

/* Representation of an <and> check (2 overrides).
 * Terminating null kept for convenience */
typedef struct {
	joseki_override_t override1;
	joseki_override_t override2;
	joseki_override_t null;
} joseki_override2_t;


/* global */
void disable_josekifix(void);
void require_josekifix(void);
bool get_josekifix_enabled(void);
bool get_josekifix_required(void);
bool josekifix_init(board_t *b);

coord_t joseki_override(struct board *b);
coord_t joseki_override_no_external_engine(struct board *b, struct ownermap *prev_ownermap, struct ownermap *ownermap);
coord_t joseki_override_external_engine_only(board_t *b);

void    josekifix_log(const char *format, ...);
bool    josekifix_ladder_setup(board_t *b, int rot, ladder_check_t *check);

/* fuseki */
coord_t josekifix_initial_fuseki(struct board *b, strbuf_t *log, hash_t lasth);

/* special checks */
coord_t josekifix_kill_3_3_invasion(struct board *b, struct ownermap *prev_ownermap, hash_t lasth);


#endif /* JOSEKIFIX */

#endif
