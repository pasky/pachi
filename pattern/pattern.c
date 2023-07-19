#define DEBUG
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern/pattern.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"
#include "tactics/1lib.h"
#include "tactics/2lib.h"
#include "tactics/util.h"
#include "playout.h"
#include "playout/moggy.h"
#include "ownermap.h"
#include "engine.h"
#include "pattern/pattern_engine.h"
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


static void check_pattern_gammas(pattern_config_t *pc);

/* For debugging: print board with move being considered */
#define show_move(b, m, msg) \
    do { \
	with_move_strict(b, m->coord, m->color, { \
		fprintf(stderr, "\n\n%s\n", msg); \
		board_print(b, stderr); \
	}); \
    } while(0)

/* Debugging. Find if pattern has given feature.
 * @payload: payload to match, or -1 to match any feature of this kind. */
static inline bool
pattern_has_feature(pattern_t *p, enum feature_id id, int payload)
{
	unsigned int payload_ = payload;
	
	assert(id < FEAT_MAX);
	for (int i = 0; i < p->n; i++) {
		feature_t *f = &p->f[i];
		if (f->id != id)  continue;
		
		if (payload == -1)		return true;
		if (f->payload == payload_)	return true;
	}
	return false;
}


feature_info_t pattern_features[FEAT_MAX];

/* For convenience */
static feature_info_t *features = pattern_features;

static void
features_init()
{
	memset(pattern_features, 0, sizeof(pattern_features));
	
	features[FEAT_CAPTURE] =         feature_info("capture",         PF_CAPTURE_N,    0);
	features[FEAT_CAPTURE2] =        feature_info("capture2",        PF_CAPTURE2_N,   0);
	features[FEAT_AESCAPE] =         feature_info("atariescape",     PF_AESCAPE_N,    0);
	features[FEAT_ATARI] =           feature_info("atari",           PF_ATARI_N,      0);
	features[FEAT_CUT] =             feature_info("cut",             PF_CUT_N,        0);
	features[FEAT_NET] =             feature_info("net",             PF_NET_N,        0);
	features[FEAT_DEFENCE] =         feature_info("defence",         PF_DEFENCE_N,    0);
	features[FEAT_WEDGE] =           feature_info("wedge",           PF_WEDGE_N,      0);
	features[FEAT_DOUBLE_SNAPBACK] = feature_info("double_snapback", 1,               0);
      features[FEAT_L1_BLUNDER_PUNISH] = feature_info("l1_blunder_punish", 1,             0);
	features[FEAT_SELFATARI] =       feature_info("selfatari",       PF_SELFATARI_N,  0);
	features[FEAT_BORDER] =          feature_info("border",          -1,              0);
	features[FEAT_DISTANCE] =        feature_info("dist",            19,              0);
	features[FEAT_DISTANCE2] =       feature_info("dist2",           19,              0);
	features[FEAT_MCOWNER] =         feature_info("mcowner",         9,               0);
	features[FEAT_NO_SPATIAL] =      feature_info("nospat",          1,               0);
	features[FEAT_SPATIAL3] =        feature_info("s3",              0,               3);
	features[FEAT_SPATIAL4] =        feature_info("s4",              0,               4);
	features[FEAT_SPATIAL5] =        feature_info("s5",              0,               5);
	features[FEAT_SPATIAL6] =        feature_info("s6",              0,               6);
	features[FEAT_SPATIAL7] =        feature_info("s7",              0,               7);
	features[FEAT_SPATIAL8] =        feature_info("s8",              0,               8);
	features[FEAT_SPATIAL9] =        feature_info("s9",              0,               9);
	features[FEAT_SPATIAL10] =       feature_info("s10",             0,               10);
}

/* Feature values may be named, otherwise payload is printed as number.
 * Names may not begin with a number. */
#define PAYLOAD_NAMES_MAX 16
static char* payloads_names[FEAT_MAX][PAYLOAD_NAMES_MAX];

static void
payloads_names_init()
{
	memset(payloads_names, 0, sizeof(payloads_names));

	payloads_names[FEAT_CAPTURE][PF_CAPTURE_ATARIDEF] = "ataridef";
	payloads_names[FEAT_CAPTURE][PF_CAPTURE_PEEP] = "peep";
	payloads_names[FEAT_CAPTURE][PF_CAPTURE_LADDER] = "ladder";
	payloads_names[FEAT_CAPTURE][PF_CAPTURE_NOLADDER] = "noladder";
	payloads_names[FEAT_CAPTURE][PF_CAPTURE_TAKE_KO] = "take_ko";
	payloads_names[FEAT_CAPTURE][PF_CAPTURE_END_KO] = "end_ko";

	payloads_names[FEAT_CAPTURE2][PF_CAPTURE2_LAST] = "last";
	
	payloads_names[FEAT_AESCAPE][PF_AESCAPE_NEW_NOLADDER] = "new_noladder";
	payloads_names[FEAT_AESCAPE][PF_AESCAPE_NEW_LADDER] = "new_ladder";
	payloads_names[FEAT_AESCAPE][PF_AESCAPE_NOLADDER] = "noladder";
	payloads_names[FEAT_AESCAPE][PF_AESCAPE_LADDER] = "ladder";
	payloads_names[FEAT_AESCAPE][PF_AESCAPE_FILL_KO] = "fill_ko";

	payloads_names[FEAT_SELFATARI][PF_SELFATARI_BAD] = "bad";
	payloads_names[FEAT_SELFATARI][PF_SELFATARI_GOOD] = "good";
	payloads_names[FEAT_SELFATARI][PF_SELFATARI_2LIBS] = "twolibs";

	payloads_names[FEAT_ATARI][PF_ATARI_DOUBLE] = "double";
	payloads_names[FEAT_ATARI][PF_ATARI_AND_CAP] = "and_cap";
	payloads_names[FEAT_ATARI][PF_ATARI_AND_CAP2] = "and_cap2";
	payloads_names[FEAT_ATARI][PF_ATARI_SNAPBACK] = "snapback";
	payloads_names[FEAT_ATARI][PF_ATARI_LADDER_BIG] = "ladder_big";
	payloads_names[FEAT_ATARI][PF_ATARI_LADDER_LAST] = "ladder_last";
	payloads_names[FEAT_ATARI][PF_ATARI_LADDER_SAFE] = "ladder_safe";
	payloads_names[FEAT_ATARI][PF_ATARI_LADDER_CUT] = "ladder_cut";
	payloads_names[FEAT_ATARI][PF_ATARI_LADDER] = "ladder";
	payloads_names[FEAT_ATARI][PF_ATARI_KO] = "ko";
	payloads_names[FEAT_ATARI][PF_ATARI_SOME] = "some";

	payloads_names[FEAT_CUT][PF_CUT_DANGEROUS] = "dangerous";
	
	payloads_names[FEAT_NET][PF_NET_LAST] = "last";
	payloads_names[FEAT_NET][PF_NET_CUT] = "cut";
	payloads_names[FEAT_NET][PF_NET_SOME] = "some";
	payloads_names[FEAT_NET][PF_NET_DEAD] = "dead";
	
	payloads_names[FEAT_DEFENCE][PF_DEFENCE_LINE2] = "line2";
	payloads_names[FEAT_DEFENCE][PF_DEFENCE_SILLY] = "silly";

	payloads_names[FEAT_WEDGE][PF_WEDGE_LINE3] = "line3";
}

static void
init_feature_info(pattern_config_t *pc)
{
	features_init();
	payloads_names_init();
	
	/* Sanity check, we use FEAT_MAX to iterate over features. */
	assert(sizeof(pattern_features) / sizeof(*pattern_features) == FEAT_MAX);

	/* Check spatial features come last */
	int first_spatial = 0;
	for (int i = 0; i < FEAT_MAX; i++)
		if (features[i].spatial)  {  first_spatial = i;  break;  }
	for (int i = 0; i < FEAT_MAX; i++)
		if (!features[i].spatial && i > first_spatial)
			die("spatial features must be last !");
	
	/* Init feature payloads */
	features[FEAT_BORDER].payloads = pc->bdist_max + 1;
	for (int i = 0; i < FEAT_MAX; i++) {
		if (features[i].spatial)
			features[i].payloads = spat_dict->nspatials_by_dist[features[i].spatial];

#ifndef GENSPATIAL
		/* Sanity check, empty features likely not a good sign ... */
		assert(features[i].payloads > 0);
#endif
	}

	/* Init gamma numbers */
	int gamma_number = 0;
	for (int i = 0; i < FEAT_MAX; i++) {
		features[i].first_gamma = gamma_number;
		gamma_number += features[i].payloads;
	}
}

int
pattern_gammas(void)
{
	return features[FEAT_MAX-1].first_gamma + feature_payloads(FEAT_MAX-1);
}

void
patterns_init(pattern_config_t *pc, char *arg, bool create, bool load_prob)
{
	char *pdict_file = NULL;
	pattern_config_t DEFAULT_PATTERN_CONFIG = { 0 };
	DEFAULT_PATTERN_CONFIG.bdist_max = 4;
	DEFAULT_PATTERN_CONFIG.spat_min = 3;
	DEFAULT_PATTERN_CONFIG.spat_max = 10;
	DEFAULT_PATTERN_CONFIG.spat_largest = false;
	
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
		prob_dict_init(pdict_file);
		/* Make sure each feature has a gamma ... */
		if (prob_dict)  check_pattern_gammas(pc);
	}
}

void
pattern_context_init(pattern_context_t *ct, pattern_config_t *pc, ownermap_t *ownermap)
{
	ct->pc = pc;
	ct->ownermap = ownermap;
}

pattern_context_t*
pattern_context_new(board_t *b, enum stone color, bool mcowner_fast)
{
	engine_t *engine = new_engine(E_PATTERN, "", b);
	pattern_config_t *pc = pattern_engine_get_pc(engine);

	pattern_context_t *ct = pattern_context_new2(b, color, pc, mcowner_fast);
	ct->engine = engine;
	return ct;
}

pattern_context_t*
pattern_context_new2(board_t *b, enum stone color, pattern_config_t *pc, bool mcowner_fast)
{
	pattern_context_t *ct = calloc2(1, pattern_context_t);
	
	ownermap_t *ownermap = malloc2(ownermap_t);	
	if (mcowner_fast)  mcowner_playouts_fast(b, color, ownermap);
	else		   mcowner_playouts(b, color, ownermap);
	
	pattern_context_init(ct, pc, ownermap);
	return ct;
}

void
pattern_context_free(pattern_context_t *ct)
{
	if (ct->engine)
		delete_engine(&ct->engine);
	free(ct->ownermap);
	free(ct);
}

#define have_last_move(b)  (!is_pass(last_move(b).coord))

static bool
is_neighbor(board_t *b, coord_t c1, coord_t c2)
{
	assert(!is_pass(c1));  	assert(!is_pass(c2));
	foreach_neighbor(b, c1, {
			if (c == c2)  return true;
	});
	return false;
}

static bool
is_neighbor_group(board_t *b, coord_t coord, group_t g)
{
	assert(!is_pass(coord));  assert(g);
	foreach_neighbor(b, coord, {
			if (group_at(b, c) == g)  return true;
	});
	return false;
}

static bool move_can_be_captured(board_t *b, move_t *m);

static int
pattern_match_capture2(board_t *b, move_t *m)
{
	if (!have_last_move(b))      return -1;
	
	enum stone other_color = stone_other(m->color);
	coord_t last_move = last_move(b).coord;

	group_t lastg = group_at(b, last_move);
	foreach_atari_neighbor(b, m->coord, other_color) {
		if (!can_capture(b, g, m->color))  continue;
		if (g == lastg)  /* Capture last move */
			return PF_CAPTURE2_LAST;
	} foreach_atari_neighbor_end;
	
	return -1;
}

static int
pattern_match_capture(board_t *b, move_t *m)
{
	enum stone other_color = stone_other(m->color);
	coord_t last_move = last_move(b).coord;
	move_queue_t can_cap;  mq_init(&can_cap);

	foreach_atari_neighbor(b, m->coord, other_color) {
		if (can_capture(b, g, m->color))
			mq_add(&can_cap, g, 0);
	} foreach_atari_neighbor_end;
	if (!can_cap.moves)  return -1;

	/* Recapture ko after playing ko-threat ? */
	if (b->last_ko_age == b->moves - 2 && m->coord == b->last_ko.coord)
		return PF_CAPTURE_TAKE_KO;
	
	if (!have_last_move(b))
		goto regular_stuff;

	/* Last move features */
	for (int i = 0; i < can_cap.moves; i++) {
		group_t capg = can_cap.move[i];
		
		/* Capture group contiguous to new group in atari ? */
		foreach_atari_neighbor(b, last_move, m->color) {
			group_t own_atari = g;
			move_queue_t q;
			countercapturable_groups(b, own_atari, &q);
			for (int i = 0; i < q.moves; i++)
				if (capg == q.move[i])
					return PF_CAPTURE_ATARIDEF;
		} foreach_atari_neighbor_end;

		/* Prevent connection to previous move ? */
		if (capg != group_at(b, last_move) &&
		    is_neighbor(b, m->coord, last_move))
			return PF_CAPTURE_PEEP;

		/* End ko by capture, ignoring ko threat ? */
		if (b->last_ko_age == b->moves - 1 && is_neighbor_group(b, last_move2(b).coord, capg))
			return PF_CAPTURE_END_KO;
	}
		
 regular_stuff:
	for (int i = 0; i < can_cap.moves; i++) {
		group_t capg = can_cap.move[i];  /* Can capture capg */
		if (is_ladder_any(b, capg, true))
			return PF_CAPTURE_LADDER;
		return PF_CAPTURE_NOLADDER;
	}
	
	return -1;
}


static int
pattern_match_aescape(board_t *b, move_t *m)
{
	enum stone other_color = stone_other(m->color);
	coord_t last_move = last_move(b).coord;
	bool found = false, ladder = false;

	if (is_selfatari(b, m->color, m->coord))
		return -1;
	
	/* Fill ko, ignoring ko-threat. */
	if (b->last_ko_age == b->moves - 1 && m->coord == b->last_ko.coord)
		return PF_AESCAPE_FILL_KO;
	
	foreach_atari_neighbor(b, m->coord, m->color) {
		ladder = is_ladder_any(b, g, true);
		found = true;
		
		/* Last move atari ? */
		if (is_pass(last_move) || last_move(b).color != other_color)  continue;
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
pattern_match_selfatari(board_t *b, move_t *m)
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
cutting_stones(board_t *b, group_t g)
{
	assert(g && group_at(b, g));
	enum stone color = board_at(b, g);
	enum stone other_color = stone_other(color);

	foreach_in_group(b, g) {
		if (neighbor_count_at(b, c, other_color) < 2)  continue;
		int x1 = coord_x(c);  int y1 = coord_y(c);
		coord_t coord = c;
		foreach_diag_neighbor(b, coord) {
			if (board_at(b, c) != color || group_at(b, c) == g)  continue;
			int x2 = coord_x(c);  int y2 = coord_y(c);
			coord_t c2 = coord_xy(x1, y2);
			coord_t c3 = coord_xy(x2, y1);
			if (board_at(b, c2) != other_color ||
			    board_at(b, c3) != other_color)   continue;
			return true;
		} foreach_diag_neighbor_end;
	} foreach_in_group_end;	
	return false;
}

/* can capture @other after atari on @atariable + defense ? */
static bool
cutting_stones_and_can_capture_other_after_atari(board_t *b, move_t *m,
						 group_t atariable, group_t other, ownermap_t *ownermap)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);

	if (ownermap_color(ownermap, other, 0.67) == color)
		return false;

	bool found = false;
	with_move(b, m->coord, m->color, {
		assert(group_at(b, atariable) == atariable);
		if (!cutting_stones(b, atariable))		break;
		if (!cutting_stones(b, other))			break;
		
		move_queue_t mq;
		coord_t lib = board_group_info(b, atariable).lib[0];
		can_countercapture(b, atariable, &mq);
		mq_add(&mq, lib, 0);
		
		/* try possible replies, must work for all of them */
		for (int i = 0; i < mq.moves; i++) {
			with_move(b, mq.move[i], other_color, {
				group_t g = group_at(b, other);
				if (g && board_group_info(b, g).libs == 2 &&
				    can_capture_2lib_group(b, g, NULL, 0))
					found = true;
				else {  found = false; mq.moves = 0;  }
			});
		}
	});
	return found;
}

/* Find suitable 2-libs target groups nearby (5x5 square)
 *   @check_capture:  that are capturable
 *   @not_in:         not in this set (optional) */
static void
atari_and_cap_find_nearby_targets(move_queue_t *targets, bool check_capturable, move_queue_t *not_in,
				  board_t *b, move_t *m, group_t atariable, ownermap_t *ownermap)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);

	mq_init(targets);

	/* Check 2-libs groups in 5x5 square around move */
	int cx = coord_x(m->coord),  cy = coord_y(m->coord);
	int x_start = MAX(cx - 2, 0), x_end = MIN(cx + 2, board_stride(b) - 1);
	int y_start = MAX(cy - 2, 0), y_end = MIN(cy + 2, board_stride(b) - 1);
	for (int x = x_start; x <= x_end; x++) {
		for (int y = y_start; y <= y_end; y++) {
			coord_t c = coord_xy(x, y);
			if (board_at(b, c) != other_color)  continue;
			
			group_t g = group_at(b, c);
			if (g == atariable ||
			    board_group_info(b, g).libs != 2 ||
			    !cutting_stones(b, g) || ownermap_color(ownermap, g, 0.67) == color ||
			    (not_in && mq_has(not_in, g)) ||
			    (check_capturable && !can_capture_2lib_group(b, g, NULL, 0)))
				continue;

			mq_add(targets, g, 0);  mq_nodup(targets);
		}
	}
}

/* Atari has been played, play defense and see if there are 2 lib targets that become capturable */
static bool
cutting_stones_and_can_capture_nearby_after_atari_(board_t *b, move_t *m, group_t atariable, ownermap_t *ownermap,
						   move_queue_t *cap_targets)
{
	bool found = false;
	enum stone color = m->color;
	enum stone other_color = stone_other(m->color);

	/* Find all 2 libs targets nearby which were not capturable initially */
	move_queue_t targets;
	atari_and_cap_find_nearby_targets(&targets, false, cap_targets, b, m, atariable, ownermap);
	if (!targets.moves)  return false;
	//fprintf(stderr, "found %i targets\n", targets.moves);
	
	/* Find possible atari answers */
	move_queue_t q;
	coord_t lib = board_group_info(b, atariable).lib[0];
	can_countercapture(b, atariable, &q);
	mq_add(&q, lib, 0);

	/* Play defense and check if we can capture any target now */
	for (int k = 0; !found && k < targets.moves; k++) {
		group_t target = group_at(b, targets.move[k]);
		
		/* Try possible answers, must work for all of them */
		for (int i = 0; i < q.moves; i++) {
			with_move(b, q.move[i], other_color, {
				group_t g = group_at(b, target);
				//fprintf(stderr, "target group %s:\n", coord2sstr(g));
				//board_print(b, stderr);

				bool can_capture_target =
					(!g ||
					 ((board_group_info(b, g).libs == 1 && can_capture(b, g, color)) ||
					  (board_group_info(b, g).libs == 2 && can_capture_2lib_group(b, g, NULL, 0))));

				group_t g2 = group_at(b, atariable);
				bool can_capture_atariable = 
					(!g2 ||
					 ((board_group_info(b, g2).libs == 1 && can_capture(b, g2, color)) ||
					  (board_group_info(b, g2).libs == 2 && can_capture_2lib_group(b, g2, NULL, 0))));
				
				//if (can_capture_target)    fprintf(stderr, "can capture target\n");
				//if (can_capture_atariable) fprintf(stderr, "can capture atariable\n");
				
				if (can_capture_target || can_capture_atariable)
					found = true;
				else {  found = false;  i = q.moves;  }  /* break for loop */
			});
		}
	}

	return found;
}

/* Can capture another group nearby after atari on @atariable + defense ? */
static bool
cutting_stones_and_can_capture_nearby_after_atari(board_t *b, move_t *m, group_t atariable, ownermap_t *ownermap)
{
	/* Note what 2-libs groups nearby are already capturable right now. */
	move_queue_t cap_targets;
	atari_and_cap_find_nearby_targets(&cap_targets, true, NULL, b, m, atariable, ownermap);
	//fprintf(stderr, "found %i capturable targets\n", cap_targets.moves);

	bool found = false;
	with_move(b, m->coord, m->color, {  /* Play atari */
		assert(group_at(b, atariable) == atariable);
		if (!cutting_stones(b, atariable))
			break;
		
		found = cutting_stones_and_can_capture_nearby_after_atari_(b, m, atariable, ownermap, &cap_targets);
	});
	return found;
}

static bool
can_countercap_common_stone(board_t *b, coord_t coord, enum stone color, group_t g1, group_t g2)
{
	int x1 = coord_x(coord);  int y1 = coord_y(coord);
	foreach_diag_neighbor(b, coord) {
		if (board_at(b, c) != color || board_group_info(b, group_at(b, c)).libs != 1)  continue;
		int x2 = coord_x(c);  int y2 = coord_y(c);
		coord_t c1 = coord_xy(x1, y2);
		coord_t c2 = coord_xy(x2, y1);
		if ((group_at(b, c1) == g1 && group_at(b, c2) == g2) ||
		    (group_at(b, c1) == g2 && group_at(b, c2) == g1))   return true;
	} foreach_diag_neighbor_end;
	return false;
}

/* Ownermap color of @coord and its neighbors if they all match, S_NONE otherwise */
static enum stone
owner_around(board_t *b, ownermap_t *ownermap, coord_t coord)
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

int
pattern_match_atari(board_t *b, move_t *m, ownermap_t *ownermap)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	group_t g1 = 0, g3libs = 0;
	bool snapback = false, double_atari = false, atari_and_cap = false, atari_and_cap2 = false;
	bool ladder_atari = false, ladder_big = false, ladder_safe = false, ladder_cut = false;
	bool ladder_last = false;

	/* Check snapback on stones we don't own already. */
	if (immediate_liberty_count(b, m->coord) == 1 && !neighbor_count_at(b, m->coord, color)) {
		with_move(b, m->coord, m->color, {
			group_t g = group_at(b, m->coord);  /* throwin stone */
			group_t atari_neighbor;
			if (g && capturing_group_is_snapback(b, g) &&
			    (atari_neighbor = board_get_atari_neighbor(b, g, other_color)) &&
			    !can_countercapture(b, atari_neighbor, NULL) &&
			    ownermap_color(ownermap, atari_neighbor, 0.67) != color)
				snapback = true;
		});
	}
	if (snapback)  return PF_ATARI_SNAPBACK;

	bool selfatari = is_selfatari(b, m->color, m->coord);
	if (selfatari && !board_is_valid_play_no_suicide(b, color, m->coord))
		return -1;	/* Check suicides (for outside callers) */
	
	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != other_color)           continue;
		group_t g = group_at(b, c);
		if (g  && board_group_info(b, g).libs == 3)  g3libs = g;
		if (!g || board_group_info(b, g).libs != 2)  continue;		
		/* Can atari! */

		/* Double atari ? */
		if (!selfatari &&
		    g1 && g != g1 && !can_countercap_common_stone(b, m->coord, color, g, g1) &&
		    board_group_other_lib(b, g, m->coord) != board_group_other_lib(b, g1, m->coord))
			double_atari = true;
		g1 = g;
		
		if (wouldbe_ladder_any(b, g, m->coord)) {
			ladder_atari = true;
			enum stone gown = ownermap_color(ownermap, g, 0.67);
			enum stone aown = owner_around(b, ownermap, m->coord);
			// capturing big group not dead yet
			if (gown != color && group_stone_count(b, g, 5) >= 3)   ladder_big = true;
			// ladder last move
			if (g == group_at(b, last_move(b).coord))               ladder_last = true;
			// capturing something in opponent territory, yummy
			if (gown == other_color && aown == other_color)         ladder_safe = true;
			// capturing cutting stones
			if (gown != color && cutting_stones(b, g))		ladder_cut = true;
		}
	});

	if (!g1)  return -1;

	/* Can capture other group after atari ? */
	if (g3libs && !selfatari && !ladder_atari &&
	    cutting_stones_and_can_capture_other_after_atari(b, m, g1, g3libs, ownermap))
		atari_and_cap = true;
	
	if (!g3libs && !selfatari && !ladder_atari &&
	    cutting_stones_and_can_capture_nearby_after_atari(b, m, g1, ownermap))
		atari_and_cap2 = true;
	
	if (ladder_big)		return PF_ATARI_LADDER_BIG;
	if (ladder_last)	return PF_ATARI_LADDER_LAST;
	if (atari_and_cap)	return PF_ATARI_AND_CAP;
	if (atari_and_cap2)	return PF_ATARI_AND_CAP2;
	if (double_atari)	return PF_ATARI_DOUBLE;
	if (ladder_safe)	return PF_ATARI_LADDER_SAFE;
	if (ladder_cut)		return PF_ATARI_LADDER_CUT;
	if (ladder_atari)	return PF_ATARI_LADDER;
	
	if (board_playing_ko_threat(b))	return PF_ATARI_KO;
	if (selfatari)			return -1;
	return PF_ATARI_SOME;
}

static int
pattern_match_border(board_t *b, move_t *m, pattern_config_t *pc)
{
	unsigned int bdist = coord_edge_distance(m->coord);
	if (bdist <= pc->bdist_max)
		return bdist;
	return -1;
}

static int
pattern_match_distance(board_t *b, move_t *m)
{
	if (is_pass(last_move(b).coord))  return -1;
	int d = coord_gridcular_distance(m->coord, last_move(b).coord);
	if (d > 17)  d = 18;
	d--; assert(d >= 0 && d <= 17);
	return d;
}

static int
pattern_match_distance2(board_t *b, move_t *m)
{
	if (is_pass(last_move2(b).coord))  return -1;
	int d = coord_gridcular_distance(m->coord, last_move2(b).coord);
	if (d > 17)  d = 17;
	/* can be zero here (same move) so don't decrement */
	assert(d >= 0 && d <= 17);
	return d;
}

static bool
safe_diag_neighbor_reaches_two_opp_groups(board_t *b, move_t *m,
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
move_can_be_captured(board_t *b, move_t *m)
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
pattern_match_cut(board_t *b, move_t *m, ownermap_t *ownermap)
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
net_can_escape(board_t *b, group_t g)
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
is_net(board_t *b, coord_t target, coord_t net)
{
	enum stone color = board_at(b, net);
	enum stone other_color = stone_other(color);
	assert(color == S_BLACK || color == S_WHITE);
	assert(board_at(b, target) == other_color);

	group_t g = group_at(b, target);
	assert(board_group_info(b, g).libs == 2);
	if (can_countercapture(b, g, NULL))  return false;  /* For now. */

	group_t netg = group_at(b, net);
	assert(board_group_info(b, netg).libs >= 2);

	bool diag_neighbors = false;
	foreach_diag_neighbor(b, net) {
		if (group_at(b, c) == g)  diag_neighbors = true;
	} foreach_diag_neighbor_end;
	assert(diag_neighbors);	

	/* Don't match on first line... */
	if (coord_edge_distance(target) == 0 ||
	    coord_edge_distance(net)    == 0)   return false;

	/* Check net shape. */
	int xt = coord_x(target),   yt = coord_y(target);
	int xn = coord_x(net),      yn = coord_y(net);
	int dx = (xn > xt ? -1 : 1),   dy = (yn > yt ? -1 : 1);

	/* Check can't escape. */
	/*  . X X .    
	 *  X O - .    -: e1, e2
	 *  X - X .    
	 *  . . . .              */
	coord_t e1 = coord_xy(xn + dx   , yn);
	coord_t e2 = coord_xy(xn        , yn + dy);
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
net_last_move(board_t *b, move_t *m, coord_t last)
{
	enum stone other_color = stone_other(m->color);

	if (last == pass)                          return false;
	if (board_at(b, last) != other_color)      return false;
	group_t lastg = group_at(b, last);
	if (board_group_info(b, lastg).libs != 2)  return false;
	if (coord_edge_distance(last) == 0)	   return false;
	
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
pattern_match_net(board_t *b, move_t *m, ownermap_t *ownermap)
{
	enum stone other_color = stone_other(m->color);
	if (immediate_liberty_count(b, m->coord) < 2)	return -1;	
	if (coord_edge_distance(m->coord) == 0)	        return -1;

	/* Speedup: avoid with_move() if there are no candidates... */
	int can = 0;
	foreach_diag_neighbor(b, m->coord) {
		if (board_at(b, c) != other_color)     continue;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs != 2)  continue;
		can++;
	} foreach_diag_neighbor_end;
	if (!can)  return -1;

	coord_t last = last_move(b).coord;
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
pattern_match_defence(board_t *b, move_t *m)
{
	enum stone other_color = stone_other(m->color);

	if (coord_edge_distance(m->coord) != 1)        return -1;
	if (immediate_liberty_count(b, m->coord) < 2)  return -1;

	foreach_neighbor(b, m->coord, {
		if (board_at(b, c) != m->color)  continue;
		if (coord_edge_distance(c) != 1) continue;
		if (neighbor_count_at(b, c, other_color) != 2)  return -1;
		if (immediate_liberty_count(b, c) != 2)  return -1;
		group_t g = group_at(b, c);
		if (board_group_info(b, g).libs != 2)  return -1;
		
		/*   . . X O .   But don't defend if we
		 *   . . O X *   can capture instead !
		 *   . . . . .
		 *  -----------  */
		int x = coord_x(c); int y = coord_y(c);
		int dx = x - coord_x(m->coord);
		int dy = y - coord_y(m->coord);
		coord_t o = coord_xy(x + dx, y + dy);
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

static bool
check_wedge_neighbors(struct board *b, coord_t coord, enum stone color)
{
	foreach_neighbor(b, coord, {
		if (coord_edge_distance(c) != 1) continue;
		if (immediate_liberty_count(b, c) < 3) return false;
	});
	return true;
}

/* -------------
 *  . . . . . .   
 *  . . . . . .  3rd line wedge that can't be blocked
 *  . X * X X .
 *  . O O O X .  
 *  . . . . . .  */
static int
pattern_match_wedge(board_t *b, move_t *m)
{
	enum stone other_color = stone_other(m->color);
	if (coord_edge_distance(m->coord) != 2)  return -1;
	if (neighbor_count_at(b, m->coord, m->color) != 1) return -1;
	if (neighbor_count_at(b, m->coord, other_color) != 2) return -1;
	
	int groups = 0;
	int found = 0;
	foreach_neighbor(b, m->coord, {
		int bdist = coord_edge_distance(c);
		group_t g = group_at(b, c);
		switch (bdist) {
			case 1: if (board_at(b, c) != S_NONE ||
				    neighbor_count_at(b, c, other_color) != 0 ||
				    neighbor_count_at(b, c, m->color)    != 0 ||
				    !check_wedge_neighbors(b, c, m->color))  return -1;
				break;
			case 3: if (board_group_info(b, g).libs <= 2)  return -1; /* short of libs */
				break;
			case 2: if (board_at(b, c) != other_color)  break;
				groups++;
				if (group_is_onestone(b, g) && board_group_info(b, g).libs <= 3)  found = 1;
				break;
			default: assert(0);
		}
	});
	
	return (groups == 2 && found ? PF_WEDGE_LINE3 : -1);
}

/*   O O X X X O O
 *   O X . X . X O
 *   O X . * . X O
 *  ---------------
 */
static int
pattern_match_double_snapback(board_t *b, move_t *m)
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
	int x = coord_x(coord);
	int y = coord_y(coord);
	with_move(b, coord, color, {
		for (int i = 0; i < 4 && snap != 2; i++) {
			snap = 0;
			for (int j = 0; j < 2; j++) {
				coord_t c = coord_xy(x + offsets[i][j][0], y + offsets[i][j][1]);
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

static coord_t
second_line_neighbor(board_t *b, coord_t coord)
{
	foreach_neighbor(b, coord, {
		if (coord_edge_distance(c) == 1)
			return c;
	});
	return pass;
}

/*  Punish silly first-line connects
 *   case 1)           case 2)          not case 1)
 *  # . . O .        . . X . . #        # . . O .
 *  # . . O .        . . . * . #        # . . . O
 *  # . . * X        O O . X . #        # . . * O
 *  # O)O O X        . . O X X)#        # . . O X
 *  # O X X X        . . O O X #        # O)O O X
 *  # X . . .        . . . . O #        # X X X X
 *  # . . . .        . . . . . #        # . . . .   */
int
pattern_match_l1_blunder_punish(board_t *b, move_t *m)
{
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	
	/* Check last move was on first line ... */
	coord_t last = last_move(b).coord;
	if (is_pass(last) || coord_edge_distance(last) != 0)  return -1;

	/* creates neighbor group with 3 libs ... */
	group_t g = group_at(b, last);
	if (!g)  return -1;
	if (board_group_info(b, g).libs != 3)   return -1;
	if (!is_neighbor_group(b, m->coord, g)) return -1;
	
	/* with one lib on each line ... */
	coord_t libs[3] = { 0, };  // libs by edge distance
	for (int i = 0; i < board_group_info(b, g).libs; i++) {
		coord_t lib = board_group_info(b, g).lib[i];
		int d = coord_edge_distance(lib);
		if (d < 3)
			libs[d] = lib;
	}
	if (!libs[0] || !libs[1] || !libs[2])  return -1;

	/* case 1) playing on 3rd-line lib */
	bool found = false;
	if (m->coord == libs[2]) {
		with_move(b, libs[2], color, {		    // we play 3rd-line lib
			group_t g2 = group_at(b, libs[2]);
			if (!g2 || board_group_info(b, g2).libs <= 2)
				break;
			coord_t below = second_line_neighbor(b, libs[2]);
			assert(below != pass);
			with_move(b, below, other_color, {    // opp plays below (may not be 2nd-line lib)
				group_t g = group_at(b, last);
				if (!g || board_group_info(b, g).libs != 2)  break;
				if (can_capture_2lib_group(b, g, NULL, 0))
					found = true;
			});
		});
	}
	
	/* case 2) check playing on 2nd-line lib */
	if (m->coord == libs[1]) {
		with_move(b, libs[1], color, {		     // we play 2nd-line lib
			/* stone must have 3 libs ... */
			if (immediate_liberty_count(b, libs[1]) != 3) break;
		    
			/* opponent group can't escape on 3rd line ... */
			bool noescape = false;
			with_move(b, libs[2], other_color, {     // opp plays 3rd-line lib
				group_t g = group_at(b, last);
				if (!g || board_group_info(b, g).libs != 2)  break;
				if (can_capture_2lib_group(b, g, NULL, 0))
					noescape = true;
			});
			if (!noescape)  break;
		    
			group_t g = group_at(b, last);
			if (!g || board_group_info(b, g).libs != 2)  break;		
			if (can_capture_2lib_group(b, g, NULL, 0))
				found = true;
		});
	}

	return (found ? 0 : -1);
}


#ifndef BOARD_SPATHASH
#undef BOARD_SPATHASH_MAXD
#define BOARD_SPATHASH_MAXD 1
#endif

/* Match spatial features that are too distant to be pre-matched
 * incrementally. Most expensive part of pattern matching, on some
 * archs this is almost 20% genmove time. Any optimization here
 * will make a big difference. */
static feature_t *
pattern_match_spatial_outer(board_t *b, move_t *m, pattern_t *p, feature_t *f,
			    pattern_config_t *pc)
{
#if 0   /* Simple & Slow */
	spatial_t s;
	spatial_from_board(pc, &s, b, m);
	int dmax = s.dist;
	for (int d = pc->spat_min; d <= dmax; d++) {
		s.dist = d;
		spatial_t *s2 = spatial_dict_lookup(d, spatial_hash(0, &s));
		if (!s2)  continue;

		f->id = FEAT_SPATIAL3 + d - 3;
		f->payload = spatial_payload(s2);
		if (!pc->spat_largest)
			(f++, p->n++);
	}
#else
	/* This is partially duplicated from spatial_from_board(),
	 * but we build a hash instead of spatial record. */
	hash_t h = pthashes[0][0][S_NONE];
	
	/* We record all spatial patterns black-to-play; simply
	 * reverse all colors if we are white-to-play. */
	static enum stone bt_black[4] = { S_NONE, S_BLACK, S_WHITE, S_OFFBOARD };
	static enum stone bt_white[4] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	enum stone *bt = m->color == S_WHITE ? bt_white : bt_black;
	int cx = coord_x(m->coord), cy = coord_y(m->coord);

	for (unsigned int d = BOARD_SPATHASH_MAXD + 1; d <= pc->spat_max; d++) {
		/* Recompute missing outer circles: Go through all points in given distance. */
		for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
			ptcoords_at(x, y, cx, cy, j);
			h ^= pthashes[0][j][bt[board_atxy(b, x, y)]];
		}
		if (d < pc->spat_min)	continue;			
		spatial_t *s = spatial_dict_lookup(d, h);
		if (!s)			continue;
		
		/* Record spatial feature, one per distance. */
		f->id = (enum feature_id)(FEAT_SPATIAL3 + d - 3);
		f->payload = spatial_payload(s);
		if (!pc->spat_largest)
			(f++, p->n++);
	}
#endif
	return f;
}

static void
pattern_match_spatial(board_t *b, move_t *m, pattern_t *p,
		      pattern_config_t *pc)
{
	if (pc->spat_max <= 0 || !spat_dict)  return;
	assert(pc->spat_min > 0);

	feature_t *f = &p->f[p->n];
	feature_t *f_orig = f;
	f->id = FEAT_NO_SPATIAL;
	f->payload = 0;

	assert(BOARD_SPATHASH_MAXD < 2);
	if (pc->spat_max > BOARD_SPATHASH_MAXD)
		f = pattern_match_spatial_outer(b, m, p, f, pc);
	if (pc->spat_largest && f->id >= FEAT_SPATIAL)		(f++, p->n++);
	if (f == f_orig) /* FEAT_NO_SPATIAL */			(f++, p->n++);
}

static int
pattern_match_mcowner(board_t *b, move_t *m, ownermap_t *o)
{
	assert(o->playouts >= MM_MINGAMES);
	int r = o->map[m->coord][m->color] * 8 / (o->playouts + 1);
	return MIN(r, 8);   // multi-threads count not exact, can reach 9 sometimes...
}

static void
mcowner_playouts_(board_t *b, enum stone color, ownermap_t *ownermap, int playouts)
{
	static playout_policy_t *policy = NULL;
	playout_setup_t setup = playout_setup(MAX_GAMELEN, 0);
	
	if (!policy)  policy = playout_moggy_init(NULL, b);
	ownermap_init(ownermap);
	
	for (int i = 0; i < playouts; i++)  {
		board_t b2;
		board_copy(&b2, b);		
		playout_play_game(&setup, &b2, color, NULL, ownermap, policy);
		board_done(&b2);
	}
	//fprintf(stderr, "pattern ownermap:\n");
	//board_print_ownermap(b, stderr, ownermap);
}

void
mcowner_playouts(board_t *b, enum stone color, ownermap_t *ownermap)
{
	mcowner_playouts_(b, color, ownermap, GJ_MINGAMES);
}

void
mcowner_playouts_fast(board_t *b, enum stone color, ownermap_t *ownermap)
{
	mcowner_playouts_(b, color, ownermap, MM_MINGAMES);
}


/* Keep track of features hits stats ? */
#ifdef PATTERN_FEATURE_STATS

static int feature_stats[FEAT_MAX][20] = { { 0, }, };
static int stats_board_positions = 0;
void pattern_stats_new_position() {  stats_board_positions++;  }

static void
dump_feature_stats(pattern_config_t *pc)
{
	static int calls = 0;
	if (++calls % 10000)  return;

	FILE *file = fopen("mm-feature-hits.dat", "w");
	if (!file)  {  perror("mm-feature-hits.dat");  return;  }
	
	fprintf(file, "feature hits:\n");
	for (int i = 0; i < FEAT_MAX; i++) {
		feature_t f = {  i  };
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
add_feature_stats(pattern_t *pattern)
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


/* For testing purposes: no prioritized features, check every feature. */
void
pattern_match_vanilla(board_t *b, move_t *m, pattern_t *pattern, pattern_context_t *ct)
{
	feature_t *f = &pattern->f[0];
	int p;  /* payload */
	pattern->n = 0;
	assert(!is_pass(m->coord));   assert(!is_resign(m->coord));

	check_feature(pattern_match_atari(b, m, ct->ownermap), FEAT_ATARI);
	check_feature(pattern_match_double_snapback(b, m), FEAT_DOUBLE_SNAPBACK);
	check_feature(pattern_match_capture(b, m), FEAT_CAPTURE);
	check_feature(pattern_match_capture2(b, m), FEAT_CAPTURE2);
	check_feature(pattern_match_aescape(b, m), FEAT_AESCAPE);
	check_feature(pattern_match_cut(b, m, ct->ownermap), FEAT_CUT);
	check_feature(pattern_match_net(b, m, ct->ownermap), FEAT_NET);
	check_feature(pattern_match_defence(b, m), FEAT_DEFENCE);
	check_feature(pattern_match_selfatari(b, m), FEAT_SELFATARI);
	check_feature(pattern_match_border(b, m, ct->pc), FEAT_BORDER);
	check_feature(pattern_match_distance(b, m), FEAT_DISTANCE);
	check_feature(pattern_match_distance2(b, m), FEAT_DISTANCE2);
	check_feature(pattern_match_mcowner(b, m, ct->ownermap), FEAT_MCOWNER);
	pattern_match_spatial(b, m, pattern, ct->pc);
}

/* TODO: We should match pretty much all of these features incrementally. */
static void
pattern_match_internal(board_t *b, move_t *m, pattern_t *pattern,
		       pattern_context_t *ct, bool locally)
{
#ifdef PATTERN_FEATURE_STATS
	dump_feature_stats(pc);
#endif

	feature_t *f = &pattern->f[0];
	int p;  /* payload */
	pattern->n = 0;
	assert(!is_pass(m->coord));   assert(!is_resign(m->coord));


	/***********************************************************************************/
	/* Prioritized features, don't let others pull them down. */
	
	check_feature(pattern_match_atari(b, m, ct->ownermap), FEAT_ATARI);
	bool atari_ladder = (p == PF_ATARI_LADDER);
	{       if (p == PF_ATARI_LADDER_BIG)  return;  /* don't let selfatari kick-in ... */
		if (p == PF_ATARI_SNAPBACK)    return;  
		if (p == PF_ATARI_AND_CAP)     return;
		if (p == PF_ATARI_AND_CAP2)    return;
		if (p == PF_ATARI_KO)          return;  /* don't let selfatari kick-in, fine as ko-threats */
	}

	check_feature(pattern_match_double_snapback(b, m), FEAT_DOUBLE_SNAPBACK);
	{	if (p == 0)  return;  }

	check_feature(pattern_match_capture2(b, m), FEAT_CAPTURE2);
	
	check_feature(pattern_match_capture(b, m), FEAT_CAPTURE); {
		if (p == PF_CAPTURE_TAKE_KO)  return;  /* don't care about distance etc */
		if (p == PF_CAPTURE_END_KO)   return;
	}

	check_feature(pattern_match_aescape(b, m), FEAT_AESCAPE);
	{	if (p == PF_AESCAPE_FILL_KO)  return;  }

	check_feature(pattern_match_cut(b, m, ct->ownermap), FEAT_CUT);
	{	if (p == PF_CUT_DANGEROUS)  return;  }

	/***********************************************************************************/
	/* Other features */

	check_feature(pattern_match_net(b, m, ct->ownermap), FEAT_NET);
	check_feature(pattern_match_defence(b, m), FEAT_DEFENCE);
	check_feature(pattern_match_wedge(b, m), FEAT_WEDGE);
	check_feature(pattern_match_l1_blunder_punish(b, m), FEAT_L1_BLUNDER_PUNISH);
	if (!atari_ladder)  check_feature(pattern_match_selfatari(b, m), FEAT_SELFATARI);
	check_feature(pattern_match_border(b, m, ct->pc), FEAT_BORDER);
	if (locally) {
		check_feature(pattern_match_distance(b, m), FEAT_DISTANCE);
		check_feature(pattern_match_distance2(b, m), FEAT_DISTANCE2);
	}
	check_feature(pattern_match_mcowner(b, m, ct->ownermap), FEAT_MCOWNER);

	pattern_match_spatial(b, m, pattern, ct->pc);
}

void
pattern_match(board_t *b, move_t *m, pattern_t *p,
	      pattern_context_t *ct, bool locally)
{
	pattern_match_internal(b, m, p, ct, locally);
	
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
payload_name(feature_t *f)
{
	if (f->payload < PAYLOAD_NAMES_MAX)
		return payloads_names[f->id][f->payload];
	return NULL;
}

char *
feature2str(char *str, feature_t *f)
{
	char *name = payload_name(f);
	if (name)  return str + sprintf(str, "%s:%s", features[f->id].name, name);
	else	   return str + sprintf(str, "%s:%d", features[f->id].name, f->payload);
}

char *
feature2sstr(feature_t *f)
{
	static char str[128];
	feature2str(str, f);
	return str;
}

/* Convert string to feature, return pointer after the featurespec. */
char *
str2feature(char *str, feature_t *f)
{
	while (isspace(*str)) str++;

	unsigned int flen = strcspn(str, ":");
	for (unsigned int i = 0; i < FEAT_MAX; i++)
		if (strlen(features[i].name) == flen && !strncmp(features[i].name, str, flen)) {
			f->id = (enum feature_id)i;
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
pattern2str(char *str, pattern_t *p)
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
pattern2sstr(pattern_t *p)
{
	static char buf[512] = { 0, };
	pattern2str(buf, p);
	return buf;
}


/* Make sure each feature has a gamma ... */
static void
check_pattern_gammas(pattern_config_t *pc)
{
	if (DEBUGL(1)) {  fprintf(stderr, "Checking gammas ...");  fflush(stderr);  }

	feature_t f;	
	for (int i = 0; i < FEAT_MAX; i++) {
		f.id = (enum feature_id)i;

		for (unsigned int j = 0; j < feature_payloads(i); j++) {
			f.payload = j;
			if (!feature_has_gamma(&f))
				die("\nNo gamma for feature (%s)\n", feature2sstr(&f));
		}
	}

	if (DEBUGL(1)) fprintf(stderr, " OK\n");
}

char *
str2pattern(char *str, pattern_t *p)
{
	p->n = 0;
	while (isspace(*str)) str++;
	if (*str++ != '(')
		die("invalid patternspec: %s\n", str);

	while (*str != ')')  str = str2feature(str, &p->f[p->n++]);

	str++;
	return str;
}
