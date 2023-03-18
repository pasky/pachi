#ifndef PACHI_JOSEKIFIX_H
#define PACHI_JOSEKIFIX_H

#ifdef JOSEKIFIX

/* Joseki / Fuseki overrides
 *
 * Allows to override engine moves based on spatial pattern around last move to
 * for example, fix joseki sequences that dcnn plays poorly.
 *
 * Overrides can either specify next move ("just override this move"), or leave it as "pass"
 * to let an external joseki engine take over the following sequence in this quadrant.
 */

struct ownermap;

/* Pattern dist used for hashes */
#define JOSEKIFIX_OVERRIDE_DIST 10

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
 * Custom override code also gets passed current ownermap and can use it in their checks.
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

	ladder_check_t ladder_check;	/* ladder checks */
	ladder_check_t ladder_check2;

#define DEFAULT_EXTERNAL_ENGINE_MOVES	15
        int external_engine_mode[4];    /* if set, external engine handles follow-up (one value per quadrant).
					 * value specifies number of external engine moves to play.
					 * note: can also just set "pass" as next move instead of filling this
					 *       (= enable for current quadrant, 15 moves)  */
} override_t;


/* global */
void disable_josekifix();
void require_josekifix();

/* loading overrides */
void josekifix_init(board_t *b);
void joseki_override_fill_hashes(override_t *override, board_t *b);
void joseki_override_print(override_t *override, char *section);
void josekifix_add_override(board_t *b, override_t *override);
void josekifix_add_override_and(board_t *b, override_t *override1, override_t *override2);
void josekifix_add_logged_variation(board_t *b, override_t *override);
void josekifix_add_logged_variation_and(board_t *b, override_t *log1, override_t *log2);

/* genmove */
coord_t joseki_override_before_genmove(board_t *b, enum stone color);
coord_t joseki_override_no_external_engine(struct board *b, struct ownermap *prev_ownermap, struct ownermap *ownermap);
coord_t joseki_override_external_engine_only(board_t *b);

/* low level override matching */
coord_t check_override(struct board *b, override_t *override, int *prot, hash_t lasth);
coord_t check_override_last(struct board *b, override_t *override, int *prot, hash_t lasth);
coord_t check_override_rot(struct board *b, override_t *override, int rot, hash_t lasth);
coord_t check_overrides(struct board *b, override_t overrides[], hash_t lasth);
coord_t check_overrides_and(struct board *b, override_t *overrides, int *prot, hash_t lasth, bool log);
bool    josekifix_sane_override(struct board *b, coord_t c, char *name, int n);
void    josekifix_log(const char *format, ...);

/* fuseki */
coord_t josekifix_initial_fuseki(struct board *b, strbuf_t *log, hash_t lasth);

/* special checks */
coord_t josekifix_kill_3_3_invasion(struct board *b, struct ownermap *prev_ownermap, hash_t lasth);

/* external joseki engine */
void    external_joseki_engine_play(coord_t c, enum stone color);
void    external_joseki_engine_fixed_handicap(int stones);
void    external_joseki_engine_forward_cmd(gtp_t *gtp, char *command);

extern char     *external_joseki_engine_cmd;
extern engine_t *external_joseki_engine;
extern int	 external_joseki_engine_genmoved;



#endif /* JOSEKIFIX */

#endif
