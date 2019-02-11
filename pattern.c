#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern.h"
#include "patternsp.h"
#include "patternprob.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"
#include "tactics/1lib.h"
#include "tactics/2lib.h"
#include "tactics/util.h"
#include "playout.h"
#include "playout/moggy.h"
#include "ownermap.h"
#include "mq.h"

/* Number of playouts for mcowner_fast().
 * Anything reliable uses much more (GJ_MINGAMES).
 * Lower this to make patternplay super fast (and mcowner even more unreliable). */
#define MM_MINGAMES 100

static bool patterns_enabled = true;
static bool patterns_required = false;
void disable_patterns()     {  patterns_enabled = false;  }
void require_patterns()     {  patterns_required = true;  }

bool
using_patterns()
{
	bool r = (patterns_enabled && spat_dict && prob_dict);
	if (patterns_required && !r)  die("patterns required but not used, aborting.\n");
	return r;
}


static void check_pattern_gammas(struct pattern_config *pc);

/* For debugging: print board with move being considered */
#define show_move(b, m, msg) \
    do { \
	with_move_strict(b, m->coord, m->color, { \
		fprintf(stderr, "\n\n%s\n", msg); \
		board_print(b, stderr); \
	}); \
    } while(0)

/* Debugging. Find if pattern has given feature.
 * @payload: -1 to match any feature of this kind or payload to match */
static inline bool
pattern_has_feature(struct pattern *p, int feature_id, int payload)
{
	assert(feature_id < FEAT_MAX);
	for (int i = 0; i < p->n; i++) {
		struct feature *f = &p->f[i];
		if (f->id != feature_id)  continue;
		
		if (payload == -1)		return true;
		if (f->payload == payload)	return true;
	}
	return false;
}

static struct pattern_config DEFAULT_PATTERN_CONFIG = {
	.bdist_max = 4,

	.spat_min = 3, .spat_max = 10,
	.spat_largest = false,
};

struct feature_info pattern_features[] = {
	[FEAT_CAPTURE] =         { .name = "capture",         .payloads = PF_CAPTURE_N,    .spatial = 0 },
	[FEAT_AESCAPE] =         { .name = "atariescape",     .payloads = PF_AESCAPE_N,    .spatial = 0 },
	[FEAT_ATARI] =           { .name = "atari",           .payloads = PF_ATARI_N,      .spatial = 0 },
	[FEAT_CUT] =             { .name = "cut",             .payloads = PF_CUT_N,        .spatial = 0 },
	[FEAT_NET] =             { .name = "net",             .payloads = PF_NET_N,        .spatial = 0 },
	[FEAT_DEFENCE] =         { .name = "defence",         .payloads = PF_DEFENCE_N,    .spatial = 0 },
	[FEAT_DOUBLE_SNAPBACK] = { .name = "double_snapback", .payloads = 1,               .spatial = 0 },
	[FEAT_SELFATARI] =       { .name = "selfatari",       .payloads = PF_SELFATARI_N,  .spatial = 0 },
	[FEAT_BORDER] =          { .name = "border",          .payloads = -1,              .spatial = 0 },
	[FEAT_DISTANCE] =        { .name = "dist",            .payloads = 19,              .spatial = 0 },
	[FEAT_DISTANCE2] =       { .name = "dist2",           .payloads = 19,              .spatial = 0 },
	[FEAT_MCOWNER] =         { .name = "mcowner",         .payloads = 9,               .spatial = 0 },
	[FEAT_NO_SPATIAL] =      { .name = "nospat",          .payloads = 1,               .spatial = 0 },
	[FEAT_SPATIAL3] =        { .name = "s3",              .payloads = -1,              .spatial = 3 },
	[FEAT_SPATIAL4] =        { .name = "s4",              .payloads = -1,              .spatial = 4 },
	[FEAT_SPATIAL5] =        { .name = "s5",              .payloads = -1,              .spatial = 5 },
	[FEAT_SPATIAL6] =        { .name = "s6",              .payloads = -1,              .spatial = 6 },
	[FEAT_SPATIAL7] =        { .name = "s7",              .payloads = -1,              .spatial = 7 },
	[FEAT_SPATIAL8] =        { .name = "s8",              .payloads = -1,              .spatial = 8 },
	[FEAT_SPATIAL9] =        { .name = "s9",              .payloads = -1,              .spatial = 9 },
	[FEAT_SPATIAL10] =       { .name = "s10",             .payloads = -1,              .spatial = 10 },
};

/* For convenience */
static struct feature_info *features = pattern_features;


/* Feature values may be named, otherwise payload is printed as number.
 * Names may not begin with a number. */
#define PAYLOAD_NAMES_MAX 16
static char* payloads_names[FEAT_MAX][PAYLOAD_NAMES_MAX] = {
	[FEAT_CAPTURE] =   { [ PF_CAPTURE_ATARIDEF] = "ataridef",
			     [ PF_CAPTURE_LAST] = "last",
			     [ PF_CAPTURE_PEEP] = "peep",
			     [ PF_CAPTURE_LADDER] = "ladder",
			     [ PF_CAPTURE_NOLADDER] = "noladder", 
			     [ PF_CAPTURE_TAKE_KO] = "take_ko",
			     [ PF_CAPTURE_END_KO] = "end_ko",
	},
	[FEAT_AESCAPE] =   { [ PF_AESCAPE_NEW_NOLADDER] = "new_noladder",
			     [ PF_AESCAPE_NEW_LADDER] = "new_ladder",
			     [ PF_AESCAPE_NOLADDER] = "noladder",
			     [ PF_AESCAPE_LADDER] = "ladder",
			     [ PF_AESCAPE_FILL_KO] = "fill_ko",
	},
	[FEAT_SELFATARI] = { [ PF_SELFATARI_BAD] = "bad",
			     [ PF_SELFATARI_GOOD] = "good",
			     [ PF_SELFATARI_2LIBS] = "twolibs",
	},
	[FEAT_ATARI] =     { [ PF_ATARI_DOUBLE] = "double",
			     [ PF_ATARI_AND_CAP] = "and_cap",
			     [ PF_ATARI_SNAPBACK] = "snapback",
			     [ PF_ATARI_LADDER_BIG] = "ladder_big",
			     [ PF_ATARI_LADDER_LAST] = "ladder_last",
			     [ PF_ATARI_LADDER_SAFE] = "ladder_safe", 
			     [ PF_ATARI_LADDER_CUT] = "ladder_cut",
			     [ PF_ATARI_LADDER] = "ladder", 
			     [ PF_ATARI_KO] = "ko",
			     [ PF_ATARI_SOME] = "some",
	},
	[FEAT_CUT] =       { [ PF_CUT_DANGEROUS] = "dangerous" },
	[FEAT_NET] =       { [ PF_NET_LAST] = "last",
			     [ PF_NET_CUT] = "cut",
			     [ PF_NET_SOME] = "some",
			     [ PF_NET_DEAD] = "dead",
	},
	[FEAT_DEFENCE] =   { [ PF_DEFENCE_LINE2] = "line2",
			     [ PF_DEFENCE_SILLY] = "silly",
	},
};

static void
init_feature_info(struct pattern_config *pc)
{
	/* Sanity check, we use FEAT_MAX to iterate over features. */
	assert(sizeof(pattern_features) / sizeof(*pattern_features) == FEAT_MAX);

	/* Init feature payloads */
	bool after_spatial = false;
	for (int i = 0; i < FEAT_MAX; i++) {
		if (features[i].spatial) {
			after_spatial = true;
			features[i].payloads = spat_dict->nspatials_by_dist[features[i].spatial];
			continue;
		} else if (after_spatial)  die("spatial features must be last !");
		
		if (i == FEAT_BORDER)  {
			features[i].payloads = pc->bdist_max + 1;
			continue;  
		}
		
		/* Regular feature */
		assert(features[i].payloads > 0);
	}
}

int
feature_payloads(enum feature_id id)
{
	assert(id < FEAT_MAX);
	assert(features[id].payloads > 0);
	return features[id].payloads;
}

void
patterns_init(struct pattern_config *pc, char *arg, bool create, bool load_prob)
{
	char *pdict_file = NULL;
	*pc = DEFAULT_PATTERN_CONFIG;

	if (!patterns_enabled)	return;
	
	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next)  *next++ = 0;  else  *next = 0;

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			/* See pattern.h:pattern_config for description and
			 * pattern.c:DEFAULT_PATTERN_CONFIG for default values
			 * of the following options. */
			if      (!strcasecmp(optname, "bdist_max") && optval)	pc->bdist_max = atoi(optval);
			else if (!strcasecmp(optname, "spat_min") && optval)	pc->spat_min = atoi(optval);
			else if (!strcasecmp(optname, "spat_max") && optval)	pc->spat_max = atoi(optval);
			else if (!strcasecmp(optname, "spat_largest"))		pc->spat_largest = !optval || atoi(optval);
			else if (!strcasecmp(optname, "pdict_file") && optval)	pdict_file = optval;
			else die("patterns: Invalid argument %s or missing value\n", optname);
		}
	}

	/* Load spatial dictionary */
	if (!spat_dict)  spatial_dict_init(pc, create);
	if (!spat_dict)  return;

	init_feature_info(pc);
	if (!load_prob)	 return;
	
	/* Load probability dictionary */
	if (!prob_dict) {
		prob_dict_init(pdict_file, pc);		
		/* Make sure each feature has a gamma ... */
		if (prob_dict)  check_pattern_gammas(pc);
	}
}


static bool
is_neighbor(struct board *b, coord_t c1, coord_t c2)
{
	assert(!is_pass(c1));  	assert(!is_pass(c2));
	foreach_neighbor(b, c1, {
			if (c == c2)  return true;
	});
	return false;
}

static bool
is_neighbor_group(struct board *b, coord_t coord, group_t g)
{
	assert(!is_pass(coord));  assert(g);
	foreach_neighbor(b, coord, {
			if (group_at(b, c) == g)  return true;
	});
	return false;
}

static bool move_can_be_captured(struct board *b, struct move *m);

static int
pattern_match_capture(struct board *b, struct move *m)
{
	enum stone other_color = stone_other(m->color);
	coord_t last_move = b->last_move.coord;
	struct move_queue atari_neighbors;
	board_get_atari_neighbors(b, m->coord, other_color, &atari_neighbors);
	if (!atari_neighbors.moves)  return -1;

	/* Recapture ko after playing ko-threat ? */
	if (b->last_ko_age == b->moves - 2 && m->coord == b->last_ko.coord)
		return PF_CAPTURE_TAKE_KO;
	
	if (is_pass(last_move) || b->last_move.color != other_color)
		goto regular_stuff;

	/* Last move features */
	for (unsigned int i = 0; i < atari_neighbors.moves; i++) {
		group_t capg = atari_neighbors.move[i];  /* Can capture capg */
		
		/* Capture group contiguous to new group in atari ? */
		foreach_atari_neighbor(b, last_move, m->color) {
			group_t own_atari = g;
			struct move_queue q;
			countercapturable_groups(b, own_atari, &q);
			for (unsigned int i = 0; i < q.moves; i++)
				if (capg == q.move[i])
					return PF_CAPTURE_ATARIDEF;
		} foreach_atari_neighbor_end;

		/* Recapture previous move ? */
		if (capg == group_at(b, last_move))
			return PF_CAPTURE_LAST;
		
		/* Prevent connection to previous move ? */
		if (capg != group_at(b, last_move) &&
		    is_neighbor(b, m->coord, last_move))
			return PF_CAPTURE_PEEP;

		/* End ko by capture, ignoring ko threat ? */
		if (b->last_ko_age == b->moves - 1 && is_neighbor_group(b, b->last_move2.coord, capg))
			return PF_CAPTURE_END_KO;
	}
		
 regular_stuff:
	for (unsigned int i = 0; i < atari_neighbors.moves; i++) {
		group_t capg = atari_neighbors.move[i];  /* Can capture capg */
		if (is_ladder_any(b, capg, true))
			return PF_CAPTURE_LADDER;
		return PF_CAPTURE_NOLADDER;
	}
	
	return -1;
}


static int
pattern_match_aescape(struct board *b, struct move *m)
{
	enum stone other_color = stone_other(m->color);
	coord_t last_move = b->last_move.coord;
	bool found = false, ladder = false;

	/* Fill ko, ignoring ko-threat. */
	if (b->last_ko_age == b->moves - 1 && m->coord == b->last_ko.coord &&
	    !is_selfatari(b, m->color, m->coord))
		return PF_AESCAPE_FILL_KO;
	
	foreach_atari_neighbor(b, m->coord, m->color) {
		ladder = is_ladder_any(b, g, true);
		found = true;
		
		/* Last move atari ? */
		if (is_pass(last_move) || b->last_move.color != other_color)  continue;
		group_t in_atari = g;
		foreach_atari_neighbor(b, last_move, m->color) {
			if (g == in_atari)
				return (ladder ? PF_AESCAPE_NEW_LADDER : PF_AESCAPE_NEW_NOLADDER);
		} foreach_atari_neighbor_end;
	} foreach_atari_neighbor_end;
	
	if (found)  return (ladder ? PF_AESCAPE_LADDER : PF_AESCAPE_NOLADDER);
	return -1;
}

static int
pattern_match_selfatari(struct board *b, struct move *m)
{
	if (is_bad_selfatari(b, m->color, m->coord))	return PF_SELFATARI_BAD;
	if (is_selfatari(b, m->color, m->coord))	return PF_SELFATARI_GOOD;
	else if (move_can_be_captured(b, m))            return PF_SELFATARI_2LIBS;
	
	return -1;
}

/*    X  Are these cutting stones ?
 *  O X  Looking for a crosscut pattern around the group.
 *  X O  XXX very naive, we don't check atari, ownership or that they belong to != groups */
static bool
cutting_stones(struct board *b, group_t g)
{
	assert(g && group_at(b, g));
	enum stone color = board_at(b, g);
	enum stone other_color = stone_other(color);

	foreach_in_group(b, g) {
		if (neighbor_count_at(b, c, other_color) < 2)  continue;
		int x1 = coord_x(c, b);  int y1 = coord_y(c, b);
		coord_t coord = c;
		foreach_diag_neighbor(b, coord) {
			if (board_at(b, c) != color || group_at(b, c) == g)  continue;
			int x2 = coord_x(c, b);  int y2 = coord_y(c, b);
			coord_t c2 = coord_xy(b, x1, y2);
			coord_t c3 = coord_xy(b, x2, y1);
			if (board_at(b, c2) != other_color ||
			    board_at(b, c3) != other_color)   continue;
			return true;
		} foreach_diag_neighbor_end;
	} foreach_in_group_end;	
	return false;
}

/* can capture @other after atari on @atariable + defense ? */
static bool
cutting_stones_and_can_capture_other_after_atari(struct board *b, struct move *m, group_t atariable, group_t other)
{
	bool found = false;
	enum stone other_color = stone_other(m->color);
	with_move(b, m->coord, m->color, {
			assert(group_at(b, atariable) == atariable);
			if (!cutting_stones(b, atariable))		break;
			if (!cutting_stones(b, other))			break;
			if (can_countercapture(b, atariable, NULL, 0))	break;
			
			coord_t lib = board_group_info(b, atariable).lib[0];
			with_move(b, lib, other_color, {
					group_t g = group_at(b, other);
					if (g && board_group_info(b, g).libs == 2 &&
					    can_capture_2lib_group(b, g, NULL, 0))
						found = true;
				});
		});
	return found;
}

static bool
can_countercap_common_stone(struct board *b, coord_t coord, enum stone color, group_t g1, group_t g2)
{
	int x1 = coord_x(coord, b);  int y1 = coord_y(coord, b);
	foreach_diag_neighbor(b, coord) {
		if (board_at(b, c) != color || board_group_info(b, group_at(b, c)).libs != 1)  continue;
		int x2 = coord_x(c, b);  int y2 = coord_y(c, b);
		coord_t c1 = coord_xy(b, x1, y2);
		coord_t c2 = coord_xy(b, x2, y1);
		if ((group_at(b, c1) == g1 && group_at(b, c2) == g2) ||
		    (group_at(b, c1) == g2 && group_at(b, c2) == g1))   return true;
	} foreach_diag_neighbor_end;
	return false;
}

/* Ownermap color of @coord and its neighbors if they all match, S_NONE otherwise */
static enum stone
owner_around(struct board *b, struct ownermap *ownermap, coord_t coord)
{
	enum stone own = ownermap_color(ownermap, coord, 0.67);
	if (own == S_NONE)  return S_NONE;
	
	foreach_neighbor(b, coord, {
			if (board_at(b, c) == S_OFFBOARD)  continue;
			enum stone own2 = ownermap_color(ownermap, c, 0.67);
			if (own2 != own)  return S_NONE;
	});
	return own;
}

static int
pattern_match_atari(struct board *b, struct move *m, struct ownermap *ownermap)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	group_t g1 = 0, g3libs = 0;
	bool snapback = false, double_atari = false, atari_and_cap = false, ladder_atari = false;
	bool ladder_big = false, ladder_safe = false, ladder_cut = false, ladder_last = false;

	/* Check snapback on stones we don't own already. */
	if (immediate_liberty_count(b, m->coord) == 1 &&
	    !neighbor_count_at(b, m->coord, m->color)) {
		with_move(b, m->coord, m->color, {
				group_t g = group_at(b, m->coord);
				group_t atari_neighbor;
				if (g && capturing_group_is_snapback(b, g) &&
				    (atari_neighbor = board_get_atari_neighbor(b, g, other_color)) &&
				    ownermap_color(ownermap, atari_neighbor, 0.67) != m->color)  // XXX check other stones in group ?)
					snapback = true;
		});
	}
	if (snapback)  return PF_ATARI_SNAPBACK;

	bool selfatari = is_selfatari(b, m->color, m->coord);
	if (!board_playing_ko_threat(b) && selfatari)  return -1;
	// XXX throw-in to capture a group with an eye isn't counted as atari with this ...

	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != other_color)           continue;
		group_t g = group_at(b, c);
		if (g  && board_group_info(b, g).libs == 3)  g3libs = g;
		if (!g || board_group_info(b, g).libs != 2)  continue;		
		/* Can atari! */

		/* Double atari ? */
		if (g1 && g != g1 && !can_countercap_common_stone(b, m->coord, color, g, g1))
			double_atari = true;
		g1 = g;
		
		if (wouldbe_ladder_any(b, g, m->coord)) {
			ladder_atari = true;
			enum stone gown = ownermap_color(ownermap, g, 0.67);
			enum stone aown = owner_around(b, ownermap, m->coord);
			// capturing big group not dead yet
			if (gown != color && group_stone_count(b, g, 5) >= 4)   ladder_big = true;
			// ladder last move
			if (g == group_at(b, b->last_move.coord))               ladder_last = true;
			// capturing something in opponent territory, yummy
			if (gown == other_color && aown == other_color)         ladder_safe = true;
			// capturing cutting stones
			if (gown != color && cutting_stones(b, g))		ladder_cut = true;
		}
	});

	if (!g1)  return -1;

	/* Can capture other group after atari ? */
	if (g3libs && !ladder_atari &&
	    ownermap_color(ownermap, g3libs, 0.67) != color &&
	    cutting_stones_and_can_capture_other_after_atari(b, m, g1, g3libs))
		atari_and_cap = true;

	if (!selfatari) {
		if (ladder_big)		return PF_ATARI_LADDER_BIG;
		if (ladder_last)        return PF_ATARI_LADDER_LAST;
		if (atari_and_cap)	return PF_ATARI_AND_CAP;
		if (double_atari)	return PF_ATARI_DOUBLE;
		if (ladder_safe)	return PF_ATARI_LADDER_SAFE;
		if (ladder_cut)		return PF_ATARI_LADDER_CUT;
		if (ladder_atari)	return PF_ATARI_LADDER;
	}
		
	if (!is_pass(b->ko.coord))	return PF_ATARI_KO;
	else				return PF_ATARI_SOME;
}

static int
pattern_match_border(struct board *b, struct move *m, struct pattern_config *pc)
{
	unsigned int bdist = coord_edge_distance(m->coord, b);
	if (bdist <= pc->bdist_max)
		return bdist;
	return -1;
}

static int
pattern_match_distance(struct board *b, struct move *m)
{
	if (is_pass(b->last_move.coord))  return -1;
	int d = coord_gridcular_distance(m->coord, b->last_move.coord, b);
	if (d > 17)  d = 18;
	d--; assert(d >= 0 && d <= 17);
	return d;
}

static int
pattern_match_distance2(struct board *b, struct move *m)
{
	if (is_pass(b->last_move2.coord))  return -1;
	int d = coord_gridcular_distance(m->coord, b->last_move2.coord, b);
	if (d > 17)  d = 17;
	/* can be zero here (same move) so don't decrement */
	assert(d >= 0 && d <= 17);
	return d;
}

static bool
safe_diag_neighbor_reaches_two_opp_groups(struct board *b, struct move *m,
					  group_t groups[4], int ngroups)
{
	enum stone other_color = stone_other(m->color);
	
	foreach_diag_neighbor(b, m->coord) {
		if (board_at(b, c) != m->color) continue;
		group_t g = group_at(b, c);

		/* Can be captured ? Not good */
		if (board_group_info(b, g).libs == 1) continue;
		if (board_group_info(b, g).libs == 2 &&
		    can_capture_2lib_group(b, g, NULL, 0)) continue;

		group_t gs[4];  memcpy(gs, groups, sizeof(gs));
		int found = 0;
		
		/* Find how many known opponent groups we reach */
		foreach_neighbor(b, c, {
				if (board_at(b, c) != other_color) continue;
				group_t g = group_at(b, c);
				for (int i = 0; i < ngroups; i++)
					if (gs[i] == g)  {  found++;  gs[i] = 0;  break;  }
			});
		if (found >= 2)  return true;
	} foreach_diag_neighbor_end;
	
	return false;
}

static bool
move_can_be_captured(struct board *b, struct move *m)
{
	if (is_selfatari(b, m->color, m->coord))
		return true;

	/* Move can be laddered ? */
	bool safe = false;
	with_move(b, m->coord, m->color, {
		group_t g = group_at(b, m->coord);
		if (!g) break;
		if (board_group_info(b, g).libs == 2 &&
		    can_capture_2lib_group(b, g, NULL, 0)) break;
		safe = true;
	});
	return !safe;
}

static int
pattern_match_cut(struct board *b, struct move *m, struct ownermap *ownermap)
{
	enum stone other_color = stone_other(m->color);
	group_t groups[4];
	int ngroups = 0;

	/* Find neighbor groups */
	foreach_neighbor(b, m->coord, {
			if (board_at(b, c) != other_color) continue;
			group_t g = group_at(b, c);
			if (board_group_info(b, g).libs <= 2) continue;  /* Not atari / capture */
			if (group_is_onestone(b, g)) continue;

			int found = 0;
			for (int i = 0; i < ngroups; i++)
				if (g == groups[i])  found = 1;
			if (found)  continue;
			
			groups[ngroups++] = g;
		});

	if (ngroups >= 2 &&
	    safe_diag_neighbor_reaches_two_opp_groups(b, m, groups, ngroups) &&
	    !move_can_be_captured(b, m)) {

		/* Cut groups short of liberties (and not prisoners) ? */
		int found = 0;
		for (int i = 0; i < ngroups; i++) {
			group_t g = groups[i];
			enum stone gown = ownermap_color(ownermap, g, 0.67);
			if (board_group_info(b, g).libs <= 3 && gown != m->color)
				found++;
		}
		if (found >= 2)
			return PF_CUT_DANGEROUS;
	}

	return -1;
}

static bool
net_can_escape(struct board *b, group_t g)
{
	assert(g);
	int libs = board_group_info(b, g).libs;
	if    (libs == 1)  return false;
	if    (libs  > 2)  return true;
	assert(libs == 2);

	bool ladder = false;
	for (int i = 0; i < 2; i++) {
		coord_t lib = board_group_info(b, g).lib[i];
		ladder |= wouldbe_ladder_any(b, g, lib);
	}
	return !ladder;
}

/* XXX move to tactics */
static bool
is_net(struct board *b, coord_t target, coord_t net)
{
	enum stone color = board_at(b, net);
	enum stone other_color = stone_other(color);
	assert(color == S_BLACK || color == S_WHITE);
	assert(board_at(b, target) == other_color);

	group_t g = group_at(b, target);
	assert(board_group_info(b, g).libs == 2);
	if (can_countercapture(b, g, NULL, 0))  return false;  /* For now. */

	group_t netg = group_at(b, net);
	assert(board_group_info(b, netg).libs >= 2);

	bool diag_neighbors = false;
	foreach_diag_neighbor(b, net) {
		if (group_at(b, c) == g)  diag_neighbors = true;
	} foreach_diag_neighbor_end;
	assert(diag_neighbors);	

	/* Don't match on first line... */
	if (coord_edge_distance(target, b) == 0 ||
	    coord_edge_distance(net, b)    == 0)   return false;

	/* Check net shape. */
	int xt = coord_x(target, b),   yt = coord_y(target, b);
	int xn = coord_x(net, b),      yn = coord_y(net, b);
	int dx = (xn > xt ? -1 : 1),   dy = (yn > yt ? -1 : 1);

	/* Check can't escape. */
	/*  . X X .    
	 *  X O - .    -: e1, e2
	 *  X - X .    
	 *  . . . .              */
	coord_t e1 = coord_xy(b, xn + dx   , yn);
	coord_t e2 = coord_xy(b, xn        , yn + dy);
	if (board_at(b, e1) != S_NONE ||
	    board_at(b, e2) != S_NONE)         return false;
	//if (board_at(b, e1) == other_color)  return false;
	//if (board_at(b, e2) == other_color)  return false;

	with_move(b, e1, other_color, {
		if (net_can_escape(b, group_at(b, target)))
			with_move_return(false);
	});
	
	with_move(b, e2, other_color, {
		if (net_can_escape(b, group_at(b, target)))
			with_move_return(false);
	});
	
	return true;
}

static bool
net_last_move(struct board *b, struct move *m, coord_t last)
{
	enum stone other_color = stone_other(m->color);

	if (last == pass)                          return false;
	if (board_at(b, last) != other_color)      return false;
	group_t lastg = group_at(b, last);
	if (board_group_info(b, lastg).libs != 2)  return false;
	if (coord_edge_distance(last, b) == 0)	   return false;
	
	bool diag_neighbors = false;
	foreach_diag_neighbor(b, last) {
		if (m->coord == c)  diag_neighbors = true;
	} foreach_diag_neighbor_end;
	if (!diag_neighbors)  return false;

	return is_net(b, last, m->coord);
}

/*  . X X    Net last move (single stone)
 *  X O .    
 *  X . *    */
static int
pattern_match_net(struct board *b, struct move *m, struct ownermap *ownermap)
{
	enum stone other_color = stone_other(m->color);
	if (immediate_liberty_count(b, m->coord) < 2)	return -1;	
	if (coord_edge_distance(m->coord, b) == 0)	return -1;

	/* Speedup: avoid with_move() if there are no candidates... */
	int can = 0;
	foreach_diag_neighbor(b, m->coord) {
		if (board_at(b, c) != other_color)     continue;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs != 2)  continue;
		can++;
	} foreach_diag_neighbor_end;
	if (!can)  return -1;

	coord_t last = b->last_move.coord;
	with_move(b, m->coord, m->color, {
		if (net_last_move(b, m, last))
			with_move_return(PF_NET_LAST);

		bool net_cut = false; bool net_some = false; bool net_dead = false;
		foreach_diag_neighbor(b, m->coord) {
			if (board_at(b, c) != other_color)     continue;
			group_t g = group_at(b, c);
			if (board_group_info(b, g).libs != 2)  continue;
			
			if (is_net(b, c, m->coord)) {
				enum stone own = ownermap_color(ownermap, c, 0.67);
				if (own != m->color && cutting_stones(b, g))	net_cut = true;
				if (own != m->color)				net_some = true;
				else						net_dead = true;
			}
		} foreach_diag_neighbor_end;
		
		if (net_cut)    with_move_return(PF_NET_CUT);
		if (net_some)   with_move_return(PF_NET_SOME);
		if (net_dead)   with_move_return(PF_NET_DEAD);
	});

	return -1;
}

/*   . . O X .   
 *   . O X * .   Defend stone on second line
 *   . . . . .
 *  -----------  */
static int
pattern_match_defence(struct board *b, struct move *m)
{
	enum stone other_color = stone_other(m->color);

	if (coord_edge_distance(m->coord, b) != 1)     return -1;
	if (immediate_liberty_count(b, m->coord) < 2)  return -1;

	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != m->color)  continue;
		if (coord_edge_distance(c, b) != 1)  continue;
		if (neighbor_count_at(b, c, other_color) != 2)  return -1;
		if (immediate_liberty_count(b, c) != 2)  return -1;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs != 2)  return -1;
		
		/*   . . X O .   But don't defend if we
		 *   . . O X *   can capture instead !
		 *   . . . . .
		 *  -----------  */
		int x = coord_x(c, b); int y = coord_y(c, b);
		int dx = x - coord_x(m->coord, b);
		int dy = y - coord_y(m->coord, b);
		coord_t o = coord_xy(b, x + dx, y + dy);
		if (board_at(b, o) != other_color)  return -1;
		group_t go = group_at(b, o);
		if (board_group_info(b, go).libs == 2 &&
		    can_capture_2lib_group(b, go, NULL, 0))
			return PF_DEFENCE_SILLY;
		
		if (can_capture_2lib_group(b, g, NULL, 0))
			return PF_DEFENCE_LINE2;
		return -1;
	});

	return -1;
}

/*   O O X X X O O
 *   O X . X . X O
 *   O X . * . X O
 *  ---------------
 */
static int
pattern_match_double_snapback(struct board *b, struct move *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);

	/* Check center spot */
	coord_t coord = m->coord;
	if (neighbor_count_at(b, coord, S_OFFBOARD) != 1 ||
	    immediate_liberty_count(b, coord) != 2 ||
	    neighbor_count_at(b, coord, other_color) != 1)  return -1;
	
	int offsets[4][2][2] = {
		{ { -1, -1}, { -1,  1} }, /* right side */
		{ {  1, -1}, {  1,  1} }, /* left       */
		{ { -1,  1}, {  1,  1} }, /* top        */
		{ { -1, -1}, {  1, -1} }  /* bottom     */
	};

	int snap = 0;
	int x = coord_x(coord, b);
	int y = coord_y(coord, b);
	with_move(b, coord, color, {
		for (int i = 0; i < 4 && snap != 2; i++) {
			snap = 0;
			for (int j = 0; j < 2; j++) {
				coord_t c = coord_xy(b, x + offsets[i][j][0], y + offsets[i][j][1]);
				if (board_at(b, c) != S_NONE)  continue;
				with_move(b, c, color, {
					group_t g = group_at(b, c);
					if (g && capturing_group_is_snapback(b, g))
						snap++;
				});
			}
		}
	});

	if (snap == 2)  return 0;
	
	return -1;
}


#ifndef BOARD_SPATHASH
#undef BOARD_SPATHASH_MAXD
#define BOARD_SPATHASH_MAXD 1
#endif

/* Match spatial features that are too distant to be pre-matched
 * incrementally. */
static struct feature *
pattern_match_spatial_outer(struct pattern_config *pc, 
                            struct pattern *p, struct feature *f,
		            struct board *b, struct move *m, hash_t h)
{
#if 0   /* Simple & Slow */
	struct spatial s;
	spatial_from_board(pc, &s, b, m);
	int dmax = s.dist;
	for (int d = pc->spat_min; d <= dmax; d++) {
		s.dist = d;
		spatial_t *s2 = spatial_dict_lookup(spat_dict, d, spatial_hash(0, &s));
		if (!s2)  continue;

		unsigned int sid = spatial_id(s2, spat_dict);
		f->id = FEAT_SPATIAL3 + d - 3;
		f->payload = sid;
		if (!pc->spat_largest)
			(f++, p->n++);
	}
#else  
	/* We record all spatial patterns black-to-play; simply
	 * reverse all colors if we are white-to-play. */
	static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
	static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	enum stone (*bt)[4] = m->color == S_WHITE ? &bt_white : &bt_black;

	for (unsigned int d = BOARD_SPATHASH_MAXD + 1; d <= pc->spat_max; d++) {
		/* Recompute missing outer circles: Go through all points in given distance. */
		for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, m->coord, b, j);
			h ^= pthashes[0][j][(*bt)[board_atxy(b, x, y)]];
		}
		if (d < pc->spat_min)	continue;			
		spatial_t *s = spatial_dict_lookup(spat_dict, d, h);
		if (!s)			continue;
		
		/* Record spatial feature, one per distance. */
		unsigned int sid = spatial_id(s, spat_dict);
		f->id = FEAT_SPATIAL3 + d - 3;
		f->payload = sid;
		if (!pc->spat_largest)
			(f++, p->n++);
	}
#endif
	return f;
}

struct feature *
pattern_match_spatial(struct pattern_config *pc, 
                      struct pattern *p, struct feature *f,
		      struct board *b, struct move *m)
{
	if (pc->spat_max <= 0 || !spat_dict)  return f;
	assert(pc->spat_min > 0);
	struct feature *orig_f = f;
	f->id = FEAT_NO_SPATIAL;
	f->payload = 0;

	/* XXX: This is partially duplicated from spatial_from_board(), but
	 * we build a hash instead of spatial record. */

	hash_t h = pthashes[0][0][S_NONE];
	assert(BOARD_SPATHASH_MAXD < 2);
	if (pc->spat_max > BOARD_SPATHASH_MAXD)
		f = pattern_match_spatial_outer(pc, p, f, b, m, h);
	if (pc->spat_largest && f->id >= FEAT_SPATIAL)		(f++, p->n++);
	if (f == orig_f) /* FEAT_NO_SPATIAL */			(f++, p->n++);
	return f;
}

static int
pattern_match_mcowner(struct board *b, struct move *m, struct ownermap *o)
{
	assert(o->playouts >= MM_MINGAMES);
	int r = o->map[m->coord][m->color] * 8 / (o->playouts + 1);
	return MIN(r, 8);   // multi-threads count not exact, can reach 9 sometimes...
}

static void
mcowner_playouts_(struct board *b, enum stone color, struct ownermap *ownermap, int playouts)
{
	static struct playout_policy *policy = NULL;
	struct playout_setup setup = { .gamelen = MAX_GAMELEN };
	
	if (!policy)  policy = playout_moggy_init(NULL, b);
	ownermap_init(ownermap);
	
	for (int i = 0; i < playouts; i++)  {
		struct board b2;
		board_copy(&b2, b);		
		playout_play_game(&setup, &b2, color, NULL, ownermap, policy);
		board_done_noalloc(&b2);
	}
	//fprintf(stderr, "pattern ownermap:\n");
	//board_print_ownermap(b, stderr, ownermap);
}

void
mcowner_playouts(struct board *b, enum stone color, struct ownermap *ownermap)
{
	mcowner_playouts_(b, color, ownermap, GJ_MINGAMES);
}

void
mcowner_playouts_fast(struct board *b, enum stone color, struct ownermap *ownermap)
{
	mcowner_playouts_(b, color, ownermap, MM_MINGAMES);
}


/* Keep track of features hits stats ? */
#ifdef PATTERN_FEATURE_STATS

static int feature_stats[FEAT_MAX][20] = { { 0, }, };
static int stats_board_positions = 0;
void pattern_stats_new_position() {  stats_board_positions++;  }

static void
dump_feature_stats(struct pattern_config *pc)
{
	static int calls = 0;
	if (++calls % 10000)  return;

	FILE *file = fopen("mm-feature-hits.dat", "w");
	if (!file)  {  perror("mm-feature-hits.dat");  return;  }
	
	fprintf(file, "feature hits:\n");
	for (int i = 0; i < FEAT_MAX; i++) {
		struct feature f = {  .id = i  };
		if (i >= FEAT_SPATIAL) continue; // For now ...
		
		/* Regular feature */
		for (int j = 0; j < feature_payloads(i); j++) {
			f.payload = j;
			fprintf(file, "  %-20s: %i\n", feature2sstr(&f), feature_stats[i][j]);
		}
	}
	fprintf(file, "  %-20s: %i    board positions: %i\n", "total calls", calls, stats_board_positions);
	fclose(file);
}

static void
add_feature_stats(struct pattern *pattern)
{
	for (int i = 0; i < pattern->n; i++) {
		int id = pattern->f[i].id;
		int p  = pattern->f[i].payload;
		if (id >= FEAT_SPATIAL)  continue;
		assert(p >= 0 && p < 20);
		feature_stats[id][p]++;
	}
}

#endif /* PATTERN_FEATURE_STATS */


#define check_feature(result, feature_id)  do { \
	p = (result); \
	if (p != -1) { \
		f->id = feature_id; \
		f->payload = p; \
		f++;  pattern->n++; \
	} \
} while (0)


/* TODO: We should match pretty much all of these features incrementally. */
static void
pattern_match_internal(struct pattern_config *pc, struct pattern *pattern, struct board *b,
		       struct move *m, struct ownermap *ownermap, bool locally)
{
#ifdef PATTERN_FEATURE_STATS
	dump_feature_stats(pc);
#endif

	struct feature *f = &pattern->f[0];
	int p;  /* payload */
	pattern->n = 0;
	assert(!is_pass(m->coord));   assert(!is_resign(m->coord));


	/***********************************************************************************/
	/* Prioritized features, don't let others pull them down. */
	
	check_feature(pattern_match_atari(b, m, ownermap), FEAT_ATARI);
	bool atari_ladder = (p == PF_ATARI_LADDER);
	{       if (p == PF_ATARI_LADDER_BIG)  return;  /* don't let selfatari kick-in ... */
		if (p == PF_ATARI_SNAPBACK)    return;  
		if (p == PF_ATARI_KO)          return;  /* don't let selfatari kick-in, fine as ko-threats */
	}

	check_feature(pattern_match_double_snapback(b, m), FEAT_DOUBLE_SNAPBACK);
	{	if (p == 0)  return;  }
	
	check_feature(pattern_match_capture(b, m), FEAT_CAPTURE); {
		if (p == PF_CAPTURE_TAKE_KO)  return;  /* don't care about distance etc */
		if (p == PF_CAPTURE_END_KO)   return;
	}

	check_feature(pattern_match_aescape(b, m), FEAT_AESCAPE);
	{	if (p == PF_AESCAPE_FILL_KO)  return;  }

	check_feature(pattern_match_cut(b, m, ownermap), FEAT_CUT);
	{	if (p == PF_CUT_DANGEROUS)  return;  }

	/***********************************************************************************/
	/* Other features */

	check_feature(pattern_match_net(b, m, ownermap), FEAT_NET);
	check_feature(pattern_match_defence(b, m), FEAT_DEFENCE);
	if (!atari_ladder)  check_feature(pattern_match_selfatari(b, m), FEAT_SELFATARI);
	check_feature(pattern_match_border(b, m, pc), FEAT_BORDER);
	if (locally) {
		check_feature(pattern_match_distance(b, m), FEAT_DISTANCE);
		check_feature(pattern_match_distance2(b, m), FEAT_DISTANCE2);
	}
	check_feature(pattern_match_mcowner(b, m, ownermap), FEAT_MCOWNER);

	f = pattern_match_spatial(pc, pattern, f, b, m);
}

void
pattern_match(struct pattern_config *pc, struct pattern *p, struct board *b,
	      struct move *m, struct ownermap *ownermap, bool locally)
{
	pattern_match_internal(pc, p, b, m, ownermap, locally);
	
	/* Debugging */
	//if (pattern_has_feature(p, FEAT_ATARI, PF_ATARI_AND_CAP))  show_move(b, m, "atari_and_cap");
	//if (pattern_has_feature(p, FEAT_NET, PF_NET_FIGHT))  show_move(b, m, "net:fight");
	//if (pattern_has_feature(p, FEAT_NET, PF_NET_SOME))   show_move(b, m, "net:some");

#ifdef PATTERN_FEATURE_STATS
	add_feature_stats(p);
#endif	
}


/* Return feature payload name if it has one. */
static char*
payload_name(struct feature *f)
{
	if (f->payload < PAYLOAD_NAMES_MAX)
		return payloads_names[f->id][f->payload];
	return NULL;
}

char *
feature2str(char *str, struct feature *f)
{
	char *name = payload_name(f);
	if (name)  return str + sprintf(str, "%s:%s", features[f->id].name, name);
	else	   return str + sprintf(str, "%s:%d", features[f->id].name, f->payload);
}

char *
feature2sstr(struct feature *f)
{
	static char str[128];
	feature2str(str, f);
	return str;
}

/* Convert string to feature, return pointer after the featurespec. */
static char *
str2feature(char *str, struct feature *f)
{
	while (isspace(*str)) str++;

	unsigned int flen = strcspn(str, ":");
	for (unsigned int i = 0; i < FEAT_MAX; i++)
		if (strlen(features[i].name) == flen && !strncmp(features[i].name, str, flen)) {
			f->id = i;
			goto found;
		}
	die("invalid featurespec: '%.*s'\n", flen, str);

found:
	str += flen + 1;
	unsigned int len = strcspn(str, ")");
	
	if (!isdigit(*str)) { /* Regular feature and name */
		for (int j = 0; j < PAYLOAD_NAMES_MAX; j++)
			if (payloads_names[f->id][j] && strlen(payloads_names[f->id][j]) == len &&
			    !strncmp(str, payloads_names[f->id][j], len)) {
				f->payload = j;
				return (str + len);
			}
		die("unknown value for feature '%s': '%.*s'\n", features[f->id].name, len, str);
	}

	/* Regular feature and number */
	f->payload = strtoull(str, &str, 10);
	return str;
}


char*
pattern2str(char *str, struct pattern *p)
{
	str = stpcpy(str, "(");
	for (int i = 0; i < p->n; i++) {
		if (i > 0) str = stpcpy(str, " ");
		str = feature2str(str, &p->f[i]);
	}
	str = stpcpy(str, ")");
	return str;
}

char*
pattern2sstr(struct pattern *p)
{
	static char buf[512] = { 0, };
	pattern2str(buf, p);
	return buf;
}


/* Make sure each feature has a gamma ... */
static void
check_pattern_gammas(struct pattern_config *pc)
{
	struct feature f;

	if (DEBUGL(1)) {  fprintf(stderr, "Checking gammas ...");  fflush(stderr);  }
	for (int i = 0; i < FEAT_MAX; i++) {
		f.id = i;

		if (i >= FEAT_SPATIAL) { 
			for (unsigned int j = 0; j < spat_dict->nspatials; j++) {
                                struct spatial *s = &spat_dict->spatials[j];
				if (!s->dist)  continue;
				assert(s->dist >= 3);
				f.id = FEAT_SPATIAL + s->dist - 3;
				f.payload = j;
				if (!feature_has_gamma(pc, &f))  goto error;
			}
			goto done;  /* Check all spatial features at once ... */
		}

		for (int j = 0; j < feature_payloads(i); j++) {
			f.payload = j;
			if (!feature_has_gamma(pc, &f))  goto error;
		}
	}

 done:
	if (DEBUGL(1)) fprintf(stderr, " OK\n");
	return;

 error:
	die("\nNo gamma for feature (%s)\n", feature2sstr(&f));
}

char *
str2pattern(char *str, struct pattern *p)
{
	p->n = 0;
	while (isspace(*str)) str++;
	if (*str++ != '(')
		die("invalid patternspec: %s\n", str);

	while (*str != ')')  str = str2feature(str, &p->f[p->n++]);

	str++;
	return str;
}
