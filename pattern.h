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

struct feature_info {
	char *name;
	int payloads;
	int spatial;   /* For spatial features, spatial feature dist */
};

extern struct feature_info pattern_features[];

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
#define PF_ATARI_DOUBLE		0  /***   Double atari */
#define PF_ATARI_AND_CAP	1  /***   Atari + can capture other group if opponent defends. */
#define PF_ATARI_SNAPBACK       2  /***** Snapback on stones we don't own already. */
#define PF_ATARI_LADDER_BIG	3  /***** Can ladder big safe opponent group */
#define PF_ATARI_LADDER_SAFE	4  /***** Can ladder safe opponent stone(s) */
#define PF_ATARI_LADDER_CUT	5  /***** Can ladder cutting stone(s) */
#define PF_ATARI_LADDER		6  /*     The atari'd group gets laddered? */
#define PF_ATARI_KO		7  /***** Atari as ko-threat ? disables selfatari feature. */
#define PF_ATARI_SOME		8  /*     Can atari something */
#define PF_ATARI_N		9

	/* Cut */
	FEAT_CUT,
#define PF_CUT_DANGEROUS	0  /***   Cut that can't be captured with shortage of libs */
#define PF_CUT_N		1

	/* Double snapback */
	FEAT_DOUBLE_SNAPBACK,      /***** Just what it says. */

	/* Border distance. */
	FEAT_BORDER,               /*     Payload: Line number, only up to 4. */

	/* Distance to last/2nd last move. */
	FEAT_DISTANCE,             /*     Payload: The distance - Up to 17. */
	FEAT_DISTANCE2,

	/* Monte-carlo owner */
	FEAT_MC_OWNER,
	
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

struct feature {
	enum feature_id id:8;
	unsigned int payload:24;
};

/* Pattern (matched) is set of features. */
struct pattern {       
	int n;
	struct feature f[FEAT_MAX];
};

struct spatial_dict;
struct prob_dict;

struct pattern_config {
	/* FEAT_BORDER: Generate features only up to this board distance. */
	unsigned int bdist_max;

	/* FEAT_SPATIAL: Generate patterns only for these sizes (gridcular).
	 * TODO: Special-case high values to match larger areas or the
	 * whole board. */
	unsigned int spat_min, spat_max;
	/* Produce only a single spatial feature per pattern, corresponding
	 * to the largest matched spatial pattern. */
	bool spat_largest;
};

/* The spatial patterns dictionary used by FEAT_SPATIAL. */
extern struct spatial_dict *spat_dict;

/* The patterns probability dictionary */
extern struct prob_dict *prob_dict;

bool using_patterns();
void disable_patterns();
void require_patterns();
void patterns_init(struct pattern_config *pc, char *arg, bool create, bool load_prob);

/* Append feature to string. */
char *feature2str(char *str, struct feature *f);
/* Feature to static string */
char *feature2sstr(struct feature *f);
/* Get number of possible payload values associated with the feature. */
int feature_payloads(enum feature_id f);

/* Append pattern as feature spec string. */
char *pattern2str(char *str, struct pattern *p);
/* Returns static string. */
char *pattern2sstr(struct pattern *p);
/* Convert string to pattern, return pointer after the patternspec. */
char *str2pattern(char *str, struct pattern *p);
/* Dump pattern as numbers for mm tools */

/* Compare two patterns for equality. Assumes fixed feature order. */
static bool pattern_eq(struct pattern *p1, struct pattern *p2);

/* Initialize p and fill it with features matched by the given board move. */
void pattern_match(struct pattern_config *pc, struct pattern *p, struct board *b, struct move *m, struct ownermap *ownermap);

/* Fill ownermap for mcowner feature. */
void mcowner_playouts(struct board *b, enum stone color, struct ownermap *ownermap);
void mcowner_playouts_fast(struct board *b, enum stone color, struct ownermap *ownermap);

#ifdef PATTERN_FEATURE_STATS
void pattern_stats_new_position();
#endif

#define feature_eq(f1, f2) ((f1)->id == (f2)->id && (f1)->payload == (f2)->payload)

static inline bool
pattern_eq(struct pattern *p1, struct pattern *p2)
{
	if (p1->n != p2->n) return false;
	for (int i = 0; i < p1->n; i++)
		if (!feature_eq(&p1->f[i], &p2->f[i]))
			return false;
	return true;
}

#endif
