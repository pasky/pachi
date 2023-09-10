#ifndef PACHI_PATTERN_H
#define PACHI_PATTERN_H

/* Matching of multi-featured patterns. */

#include "board.h"
#include "ownermap.h"
#include "move.h"

/* When someone says "pattern", you imagine a configuration of stones in given
 * area (e.g. as matched very efficiently by pattern3 in case of 3x3 area).
 * However, we use a richer definition of pattern, where this is merely one
 * pattern _feature_. Another features may be is-a-selfatari, is-a-capture,
 * number of liberties, distance from last move, etc.
 *
 * Each feature is represented by its id and an optional 32-bit payload;
 * when matching, discrete (id,payload) pairs are considered.
 *
 * This is heavily influenced by (Coulom, 2007), of course. In addition,
 * the work of van der Werf, de Groot, Stern et al. and possibly others
 * inspired this pattern matcher.
 *
 * See the HACKING file for another description of the pattern matcher and
 * instructions on how to harvest and inspect patterns. */


/* Keep track of features hits stats ?
 * Useful to get an idea how much a given feature matches / compares to others.
 * Output written to mm-feature-hits.dat periodically. */
//#define PATTERN_FEATURE_STATS 1

typedef struct {
	char        *name;
	unsigned int payloads;
	int          spatial;		/* For spatial features, spatial feature dist */
	int          first_gamma;	/* gamma numbers for this feature start from here */
} feature_info_t;

extern feature_info_t pattern_features[];

/* If you add a payload for a feature, don't forget to update the values in feature_info. 
 * Legend:                          *      Ordinary feature
 *                                  ***    Feature with artificial gamma
 *                                  *****  Prioritized feature, possibly with artificial gamma */
enum feature_id {
	/* Capture */
	FEAT_CAPTURE,	
#define PF_CAPTURE_ATARIDEF	0  /*     Capture group contiguous to new group in atari */	
#define PF_CAPTURE_LAST		1  /*     Recapture previous move */
#define PF_CAPTURE_PEEP		2  /*     Prevent connection to previous move */
#define PF_CAPTURE_LADDER	3  /*     Capturing group already in a ladder */
#define PF_CAPTURE_NOLADDER	4  /*     Capturing group not in a ladder */
#define PF_CAPTURE_TAKE_KO	5  /***** Recapture ko after ko-threat */
#define PF_CAPTURE_END_KO	6  /***** End ko by capturing something else */
#define PF_CAPTURE_N		7

	/* Atari escape (extension). */
	FEAT_AESCAPE,	
#define PF_AESCAPE_NEW_NOLADDER	0  /*     Escape new atari, not in a ladder */
#define PF_AESCAPE_NEW_LADDER	1  /*     Escape new atari, in a ladder */
#define PF_AESCAPE_NOLADDER	2  /*     Escape atari, not in a ladder */
#define PF_AESCAPE_LADDER	3  /*     Escape atari, in a ladder */
#define PF_AESCAPE_FILL_KO	4  /***** Fill ko, ignoring ko threat */
#define PF_AESCAPE_N		5

	/* Self-atari move. */
	FEAT_SELFATARI,
#define PF_SELFATARI_BAD	0  /*      Bad selfatari (tries to be aware of nakade, throwins, ...) */
#define PF_SELFATARI_GOOD	1  /*      Move is selfatari, and it's not bad. */
#define PF_SELFATARI_2LIBS      2  /*      Creates 2-libs group that can be captured (ladder) */
#define PF_SELFATARI_N		3

	/* Atari move. */
	FEAT_ATARI,
#define PF_ATARI_SNAPBACK       0  /***** Snapback on stones we don't own already. */
#define PF_ATARI_LADDER_BIG	1  /***** Can ladder big safe opponent group */
#define PF_ATARI_LADDER_LAST    2  /*     Ladder last move */
#define PF_ATARI_AND_CAP	3  /***   Atari + can capture other group if opponent defends. */
#define PF_ATARI_AND_CAP2	4  /***   Atari + can capture other group if opponent defends. */
#define PF_ATARI_DOUBLE		5  /***   Double atari */
#define PF_ATARI_LADDER_SAFE	6  /***** Can ladder safe opponent stone(s) */
#define PF_ATARI_LADDER_CUT	7  /*     Can ladder cutting stone(s) */
#define PF_ATARI_LADDER		8  /*     The atari'd group gets laddered? */
#define PF_ATARI_KO		9  /***** Atari as ko-threat ? disables selfatari feature. */
#define PF_ATARI_SOME		10 /*     Can atari something */
#define PF_ATARI_N		11

	/* Net */
	FEAT_NET,
#define PF_NET_LAST		0  /***   Capture last move in net (single stone) */
#define PF_NET_CUT		1  /***   Net cutting stone (not already owned by us) */
#define PF_NET_SOME		2  /***   Net something     (not already owned by us) */
#define PF_NET_DEAD		3  /*     Net something     (own territory) */
#define PF_NET_N		4

	/* 2nd line defence */
	FEAT_DEFENCE,
#define PF_DEFENCE_LINE2	0  /***   Defend stone on second line */
#define PF_DEFENCE_SILLY	1  /***   Can cap instead */
#define PF_DEFENCE_N		2

	/* Cut */
	FEAT_CUT,
#define PF_CUT_DANGEROUS	0  /***** Cut that can't be captured with shortage of libs around */
#define PF_CUT_N		1

	FEAT_WEDGE,
#define	PF_WEDGE_LINE3		0  /*     3rd line wedge that can't be blocked */
#define PF_WEDGE_N		1

	/* First line blunder */
	FEAT_L1_BLUNDER_PUNISH,	   /***   Punish first line blunder (connect and short of liberties) */
	
	/* Double snapback */
	FEAT_DOUBLE_SNAPBACK,      /***** Just what it says. */

	/* Border distance. */
	FEAT_BORDER,               /*     Payload: Line number, only up to 4. */

	/* Distance to last/2nd last move. */
	FEAT_DISTANCE,             /*     Payload: The distance - Up to 17. */
	FEAT_DISTANCE2,

	/* Monte-carlo owner */
	FEAT_MCOWNER,
	
	/* Spatial configuration of stones in certain board area,
	 * with black to play. */
	/* Payload: Index in the spatial_dict. */
	FEAT_NO_SPATIAL,
	FEAT_SPATIAL3,
	FEAT_SPATIAL4,
	FEAT_SPATIAL5,
	FEAT_SPATIAL6,
	FEAT_SPATIAL7,
	FEAT_SPATIAL8,
	FEAT_SPATIAL9,
	FEAT_SPATIAL10,

	FEAT_MAX
};

/* Having separate spatial features is nice except for this ... */
#define FEAT_SPATIAL FEAT_SPATIAL3

typedef struct {
	enum feature_id id;
	unsigned int    payload;
} feature_t;

#define feature(id, payload)  {  (enum feature_id)(id), (payload)  }

/* Pattern (matched) is set of features. */
typedef struct {
	int n;
	feature_t f[FEAT_MAX];
} pattern_t;

typedef struct {
	/* FEAT_BORDER: Generate features only up to this board distance. */
	unsigned int bdist_max;

	/* FEAT_SPATIAL: Generate patterns only for these sizes (gridcular).
	 * TODO: Special-case high values to match larger areas or the
	 * whole board. */
	unsigned int spat_min, spat_max;
	/* Produce only a single spatial feature per pattern, corresponding
	 * to the largest matched spatial pattern. */
	bool spat_largest;
} pattern_config_t;


/* Common pre-computed data-structures before matching individual patterns */
typedef struct {
	pattern_config_t *pc;
	ownermap_t *ownermap;
	struct engine *engine;	/* optional, pattern_context_new() only */
} pattern_context_t;

bool using_patterns();
void disable_patterns();
void require_patterns();
void patterns_init(pattern_config_t *pc, char *arg, bool create, bool load_prob);

/* Append feature to string. */
char *feature2str(char *str, feature_t *f);
/* Feature to static string */
char *feature2sstr(feature_t *f);
/* String to feature */
char *str2feature(char *str, feature_t *f);
/* Get number of possible payload values associated with the feature. */
#define feature_payloads(id)  (pattern_features[id].payloads)
/* Get gamma number for feature */
static int feature_gamma_number(feature_t *f);
/* Get total number of gammas for all features */
int pattern_gammas(void);

/* Append pattern as feature spec string. */
char *pattern2str(char *str, pattern_t *p);
/* Returns static string. */
char *pattern2sstr(pattern_t *p);
/* Convert string to pattern, return pointer after the patternspec. */
char *str2pattern(char *str, pattern_t *p);
/* Dump pattern as numbers for mm tools */

/* Compare two patterns for equality. Assumes fixed feature order. */
static bool pattern_eq(pattern_t *p1, pattern_t *p2);

/* Initialize context from existing parts. */
void pattern_context_init(pattern_context_t *ct, pattern_config_t *pc, ownermap_t *ownermap);
/* Allocate and setup new context and all required parts (expensive) */
pattern_context_t *pattern_context_new(board_t *b, enum stone color, bool mcowner_fast);
/* Same if you already have a pattern config */
pattern_context_t *pattern_context_new2(board_t *b, enum stone color, pattern_config_t *pc, bool mcowner_fast);
/* Free context created with pattern_context_new() */
void pattern_context_free(pattern_context_t *ct);

/* Initialize p and fill it with features matched by the given board move. 
 * @locally: Looking for local moves ? Distance features disabled if false. */
void pattern_match(board_t *b, move_t *m, pattern_t *p, pattern_context_t *ct, bool locally);
/* For testing purposes: no prioritized features, check every feature. */
void pattern_match_vanilla(board_t *b, move_t *m, pattern_t *p, pattern_context_t *ct);

/* Fill ownermap for mcowner feature. */
void mcowner_playouts(board_t *b, enum stone color, ownermap_t *ownermap);
/* Faster version with few playouts, don't use for anything reliable. */
void mcowner_playouts_fast(board_t *b, enum stone color, ownermap_t *ownermap);

/* Low-level functions for unit-tests and outside tactical checks */
int pattern_match_l1_blunder_punish(board_t *b, move_t *m);
int pattern_match_atari(board_t *b, move_t *m, ownermap_t *ownermap);


#ifdef PATTERN_FEATURE_STATS
void pattern_stats_new_position();
#endif

#define feature_eq(f1, f2) ((f1)->id == (f2)->id && (f1)->payload == (f2)->payload)

static inline feature_info_t
feature_info(char *name, int payloads, int spatial)
{
       feature_info_t f = { name, payloads, spatial };
       return f;
}

static inline bool
pattern_eq(pattern_t *p1, pattern_t *p2)
{
	if (p1->n != p2->n) return false;
	for (int i = 0; i < p1->n; i++)
		if (!feature_eq(&p1->f[i], &p2->f[i]))
			return false;
	return true;
}

static inline int
feature_gamma_number(feature_t *f)
{
	assert(f->payload < feature_payloads(f->id));
	return pattern_features[f->id].first_gamma + f->payload;
}


#endif
