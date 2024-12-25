#include <assert.h>
#include <stdarg.h>

#define DEBUG
#include "board.h"
#include "debug.h"
#include "pattern/spatial.h"
#include "josekifix/override.h"
#include "josekifix/joseki_override.h"


/*****************************************************************************/
/* board_print_pattern(): visualize pattern area */

/*
typedef struct {
	coord_t coord;
	unsigned int d;
} t_print_pattern_data;

static char*
print_pattern_handler(struct board *b, coord_t c, void *pdata)
{
	t_print_pattern_data *data = pdata;
	int cx = coord_x(data->coord);    int cy = coord_y(data->coord);
	
	for (unsigned int d = 2; d <= data->d; d++)
	for (unsigned int j = ptind[d]; j < ptind[d + 1]; j++) {
		ptcoords_at(x, y, cx, cy, j);
		if (coord_xy(b, x, y) == c)
			return "*";
	}
	
	static char s[2] = { 0, };
	s[0] = stone2char(board_at(b, c));
	return s;
}

static void
board_print_pattern_full(struct board *b, coord_t coord, unsigned int d)
{
	t_print_pattern_data data = { .coord = coord, .d = d };
	board_hprint(b, stderr, print_pattern_handler, (void*)&data);
}

static void
board_print_pattern(struct board *b, coord_t coord)
{
	board_print_pattern_full(b, coord, MAX_PATTERN_DIST);
}
*/


/*****************************************************************************/
/* Low-level override matching */

static coord_t
str2coord_safe(char *str)
{
	if (!str || !str[0])  return pass;
	return str2coord(str);
}

/* Check override at given location (single rotation) */
static coord_t
check_override_at_rot(struct board *b, override_t *override,
		      int rot, char* coordstr, enum stone stone_color)
{
	assert(override->next[0] && override->next[0] != 'X');
	assert(coordstr[0] && coordstr[0] != 'X');

	coord_t coord = str2coord(coordstr);
	coord_t prev  = str2coord_safe(override->prev);  // optional
	coord_t next = str2coord(override->next);
	
	if (!is_pass(prev) && rotate_coord(prev, rot) != last_move(b).coord)  return pass;
	assert(!is_pass(next));
	
	coord_t rcoord = rotate_coord(coord, rot);
	if (board_at(b, rcoord) == stone_color) {
		hash_t h = outer_spatial_hash_from_board(b, rcoord, last_move(b).color); /* hash with last move color */
		if (h == override->hashes[rot])
			return rotate_coord(next, rot);
	}
	return pass;
}

/* Check override at given location (all rotations)
 * Rotation found written to @prot in case of match. */
static coord_t
check_override_at(struct board *b, override_t *override,
		  int *prot, char* coordstr, enum stone stone_color)
{
	for (int rot = 0; rot < 8; rot++) {
		coord_t c = check_override_at_rot(b, override, rot, coordstr, stone_color);
		if (!is_pass(c)) {
			if (prot)  *prot = rot;
			return c;
		}
	}
	
	return pass;
}

/* Check override around last move (single rotation) */
static coord_t
check_override_last_rot(struct board *b, override_t *override, int rot, hash_t lasth)
{
	assert(override->prev[0] && override->prev[0] != 'X');
	assert(override->next[0] && override->next[0] != 'X');

	coord_t prev = str2coord(override->prev);
	coord_t next = str2coord(override->next);
	
	if (rotate_coord(prev, rot) != last_move(b).coord)  return pass;
	assert(!is_pass(next));
	
	if (lasth == override->hashes[rot])
		return rotate_coord(next, rot);
	return pass;
}

/* Check override around last move (all rotations)
 * Rotation found written to @prot in case of match */
coord_t
check_override_last(struct board *b, override_t *override, int *prot, hash_t lasth)
{
	for (int rot = 0; rot < 8; rot++) {
	    coord_t c = check_override_last_rot(b, override, rot, lasth);
	    if (!is_pass(c)) {
		    if (prot)  *prot = rot;
		    return c;
	    }
	}
	
	return pass;
}

/* Check and warn if returned move is not sane... */
/* XXX check what happens with logging  (special fuseki bad move) */
bool
sane_override_move(struct board *b, coord_t c, char *name, char *title)
{
	enum stone color = stone_other(last_move(b).color);
	if (is_pass(c))  return true;
	if (!board_is_valid_play_no_suicide(b, color, c) && DEBUGL(0)) {
		josekifix_log("%s (move %i): %s (%s)  WARNING invalid move !!\n", title, b->moves, coord2sstr(c), name);
		return false;
	}
	return true;
}

coord_t
check_override_rot(struct board *b, override_t *override, int rot, hash_t lasth)
{
	enum stone color = last_move(b).color;
	if (override->coord_other)  return check_override_at_rot(b, override, rot, override->coord_other, color);
	if (override->coord_own)    return check_override_at_rot(b, override, rot, override->coord_own, stone_other(color));
	if (override->coord_empty)  return check_override_at_rot(b, override, rot, override->coord_empty, S_NONE);
	return check_override_last_rot(b, override, rot, lasth);
}

static coord_t
check_override_(struct board *b, override_t *override, int *prot, hash_t lasth)
{
	enum stone color = last_move(b).color;
	if (override->coord_other)  return check_override_at(b, override, prot, override->coord_other, color);
	if (override->coord_own)    return check_override_at(b, override, prot, override->coord_own, stone_other(color));
	if (override->coord_empty)  return check_override_at(b, override, prot, override->coord_empty, S_NONE);
	return check_override_last(b, override, prot, lasth);
}

/* Check single override, making sure returned move is sane. */
coord_t
check_override(struct board *b, override_t *override, int *prot, hash_t lasth, char *title)
{
	coord_t c = check_override_(b, override, prot, lasth);

	/* Check move is sane... */
	if (!sane_override_move(b, c, override->name, title))
		return pass;
	
	return c;
}

/* Check overrides, return first match's move (pass if none).
 * Matching needs not be optimized at all (few entries, running once per genmove).
 * So we just run through the whole list checking each one (we have hashes for all rotations). */
coord_t
check_overrides(struct board *b, override_t overrides[], hash_t lasth, char *title)
{
	if (!overrides)  return pass;

	for (int i = 0; overrides[i].name; i++) {
		override_t *override = &overrides[i];
		coord_t c = check_override(b, override, NULL, lasth, title);
		if (is_pass(c))
			continue;

		/* No logging here. */

		/* Return first match */
		return c;
	}

	return pass;
}


