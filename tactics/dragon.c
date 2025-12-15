#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define QUICK_BOARD_CODE

//#define DEBUG
#include "board.h"
#include "board_undo.h"
#include "debug.h"
#include "tactics/dragon.h"
#include "tactics/nakade.h"
#include "tactics/ladder.h"
#include "tactics/selfatari.h"

static char*
print_handler(board_t *board, coord_t c, void *data)
{
	static char buf[64];
	group_t dragon = *(group_t*)data;
	char *before = "", *after = "";
	if (dragon_at(board, c) == dragon) {
		before = "\x1b[40;33;1m";
		after = "\x1b[0m";
	}
	sprintf(buf, "%s%c%s", before, stone2char(board_at(board, c)), after);
	return buf;
}

void
dragon_print(board_t *board, FILE *f, group_t dragon)
{
        board_hprint(board, f, print_handler, &dragon);
}

static char *bold_colors[] = {
	"\x1b[40;33;1m", // gold
	"\x1b[40;32;1m", // green
	"\x1b[40;31;1m", // red
	"\x1b[40;34;1m", // blue
	"\x1b[40;35;1m", // purple
	"\x1b[40;36;1m", // lblue
	"\x1b[40;37;1m", // white
};

static char *normal_colors[] = {
	"\x1b[40;33m", // gold
	"\x1b[40;32m", // green
	"\x1b[40;31m", // red
	"\x1b[40;34m", // blue
	"\x1b[40;35m", // purple
	"\x1b[40;36m", // lblue
	"\x1b[40;37m", // white, must be last
};

static char *ansi_color_end = "\x1b[0m";

char *
pick_dragon_color(int i, bool bold, bool white_ok)
{
	int ncolors = sizeof(normal_colors) / sizeof(char *);
	if (!white_ok)
		ncolors--;
	if (bold)
		return bold_colors[i % ncolors];
	return normal_colors[i % ncolors];
}

static char*
print_dragons(board_t *board, coord_t c, void *data)
{
	static char buf[64];
	group_t *dragons = (group_t*)data;
	group_t d = dragon_at(board, c);
	char *before = "", *after = "";

	if (d) {
		int i;  // Dragon index
		for (i = 0; dragons[i] && dragons[i] != d; i++)
			;
		if (!dragons[i])  {  dragons[i] = d;  }  /* Add new */			
		
		before = pick_dragon_color(i, (c == d), true);  // Dragon base: bold
		after = ansi_color_end;
	}
		
	sprintf(buf, "%s%c%s", before, stone2char(board_at(board, c)), after);
	return buf;
}

void
board_print_dragons(board_t *board, FILE *f)
{
	group_t dragons[BOARD_MAX_GROUPS] = { 0, };
        board_hprint(board, f, print_dragons, dragons);
}

#define no_stone_at(c)          (board_at(b, (c)) == S_NONE)
#define own_stone_at(c)         (board_at(b, (c)) == color)
#define enemy_stone_at(c)       (board_at(b, (c)) == stone_other(color))
#define no_stone_atxy(x, y)     (board_atxy(b, (x), (y)) == S_NONE)
#define own_stone_atxy(x, y)    (board_atxy(b, (x), (y)) == color)
#define enemy_stone_atxy(x, y)  (board_atxy(b, (x), (y)) == stone_other(color))

/* Find real liberties, not just pseudo-liberties. */
static void
get_group_liberties(board_t *b, group_t g, mq_t *q)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, g));
#endif
	/* Use pseudo-liberties if possible */
	if (group_libs(b, g) <= GROUP_REFILL_LIBS) {
		q->moves = group_libs(b, g);
		memcpy(q->move, b->gi[g].lib, sizeof(coord_t) * q->moves);
		return;
	}

	/* Otherwise go and find all liberties */
	mq_init(q);
	foreach_in_group(b, g) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != S_NONE || mq_has(q, c))
				continue;
			mq_add(q, c);
		});
	} foreach_in_group_end;
}

/* Check if g and g2 are virtually connected through lib.
 * c2 is a stone of g2 next to lib */
static bool
virtual_connection_at(board_t *b, enum stone color, coord_t lib, coord_t c2, group_t g1, group_t g2)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(board_at(b, lib) == S_NONE);
	assert(board_at(b, c2) == color);
	assert(group_at(b, c2) == g2);
	assert(sane_group(b, g1));
	assert(sane_group(b, g2));
#endif
	/* Eye / Hanging connection ? */
	if (is_controlled_eye_point(b, lib, color))
		return true;

	/* Diagonal connection ? */
	int x2 = coord_x(c2),          y2 = coord_y(c2);
	foreach_diag_neighbor(b, c2, {
		if (board_at(b, c) != color || group_at(b, c) != g1)
			continue;
		int x = coord_x(c);    coord_t d1 = coord_xy(x, y2);
		int y = coord_y(c);    coord_t d2 = coord_xy(x2, y);		   
		if (no_stone_at(d1) && no_stone_at(d2))
			return true;	
	});

	int x = coord_x(lib);          int dx = coord_dx(lib, c2);
	int y = coord_y(lib);          int dy = coord_dy(lib, c2);
	int x1 = x + dx;
	int y1 = y + dy;
	coord_t c1 = coord_xy(x1, y1);  // other side of lib wrt c2
	
	/* Bamboo joint or stronger ? */
	if ( own_stone_at(c1) && group_at(b, c1) == g1 && 
	     ( (!dx && own_stone_atxy(x1-1, y1) && own_stone_atxy(x2-1, y2) && !enemy_stone_atxy(x-1, y)) ||
	       (!dx && own_stone_atxy(x1+1, y1) && own_stone_atxy(x2+1, y2) && !enemy_stone_atxy(x+1, y)) ||
	       (!dy && own_stone_atxy(x1, y1-1) && own_stone_atxy(x2, y2-1) && !enemy_stone_atxy(x, y-1)) ||
	       (!dy && own_stone_atxy(x1, y1+1) && own_stone_atxy(x2, y2+1) && !enemy_stone_atxy(x, y+1))   ))
		return true;

	/* TODO more fancy stuff ... */
			  
	return false;	
}



/* Handler should return -1 to stop iterating */
typedef int (*foreach_in_connected_groups_t)(board_t *b, enum stone color, coord_t c, void *data);

static int
foreach_in_connected_groups_(board_t *b, enum stone color, group_t g, 
			     foreach_in_connected_groups_t f, void *data, int *visited)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_group(b, g));
	assert(board_at(b, g) == color);
#endif
	if (visited[g])
		return 0;
	visited[g] = 1;

	foreach_in_group(b, g) {
		if (f(b, color, c, data) == -1)
			return -1;
	} foreach_in_group_end;	

	/* Look for virtually connected groups. */
	mq_t q;  get_group_liberties(b, g, &q);
	for (int i = 0; i < q.moves; i++) {
		coord_t lib = q.move[i];
		// TODO could mark liberties visited, more efficient ?
		foreach_neighbor(b, lib, {
			if (board_at(b, c) != color)
				continue;
			group_t g2 = group_at(b, c);
			if (visited[g2] || !virtual_connection_at(b, color, lib, c, g, g2))
				continue;
			if (foreach_in_connected_groups_(b, color, g2, f, data, visited) == -1)
				return -1;
		});
	}
	return 0;
}

/* Call f() for each stone in dragon at @to. */
static void 
foreach_in_connected_groups(board_t *b, enum stone color, coord_t to, 
			    foreach_in_connected_groups_t f, void *data)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == color);
#endif
	int visited[BOARD_MAX_COORDS] = {0, };
	group_t g = group_at(b, to);
	foreach_in_connected_groups_(b, color, g, f, data, visited);
}


/* Handler should return -1 to stop iterating. */
typedef int (*foreach_connected_group_t)(board_t *b, enum stone color, group_t g, void *data);

static int
foreach_connected_group_(board_t *b, enum stone color, group_t g, 
			 foreach_connected_group_t f, void *data, int *visited)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_group(b, g));
	assert(board_at(b, g) == color);
#endif
	if (visited[g])
		return 0;

	visited[g] = 1;
	if (f(b, color, g, data) == -1)
		return -1;

	/* Look for virtually connected groups. */
	mq_t q;  get_group_liberties(b, g, &q);
	for (int i = 0; i < q.moves; i++) {
		coord_t lib = q.move[i];
		// TODO could mark liberties visited, more efficient ?
		foreach_neighbor(b, lib, {
			if (board_at(b, c) != color)
				continue;
			group_t g2 = group_at(b, c);
			if (visited[g2] || !virtual_connection_at(b, color, lib, c, g, g2))
				continue;
			if (foreach_connected_group_(b, color, g2, f, data, visited) == -1)
				return -1;
		});
	}
	return 0;
}

/* Call f() for each group in dragon at @to. */
static void 
foreach_connected_group(board_t *b, enum stone color, coord_t to,
			foreach_connected_group_t f, void *data)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == color);
#endif
	int visited[BOARD_MAX_COORDS] = {0, };
	group_t g = group_at(b, to);
	foreach_connected_group_(b, color, g, f, data, visited);
}

typedef struct {
	int *visited;
	foreach_in_connected_groups_t f;
	void *data;
} foreach_lib_data_t;

static int 
foreach_lib_handler(board_t *b, enum stone color, group_t g, void *data)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_group(b, g));
	assert(board_at(b, g) == color);
#endif
	foreach_lib_data_t *d = (foreach_lib_data_t*)data;
	mq_t q;  get_group_liberties(b, g, &q);
	for (int i = 0; i < q.moves; i++) {
		coord_t lib = q.move[i];
		if (d->visited[lib])
			continue;
		d->visited[lib] = 1;
		if (d->f(b, color, lib, d->data) == -1)
			return -1;
	}
	return 0;			
}

/* Call f() for each liberty of dragon at @to. */
static void
foreach_lib_in_connected_groups(board_t *b, enum stone color, coord_t to,
				foreach_in_connected_groups_t f, void *data)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == color);
#endif
	int visited[BOARD_MAX_COORDS] = {0, };
	foreach_lib_data_t d = { visited, f, data };
	/* Use foreach_connected_group() instead of foreach_in_connected_group():
	 * may avoid iterating through group stones if pseudo-liberties are valid. */
	foreach_connected_group(b, color, to, foreach_lib_handler, &d);
}


static int
stones_all_connected_handler(board_t *b,  enum stone color, coord_t c, void *data)
{
	int *connected = (int*)data;
	connected[c] = 1;  return 0;
}

static bool
stones_all_connected(board_t *b, enum stone color, mq_t *stones)
{
	// TODO optimize: check if all same group first ...
	int connected[BOARD_MAX_COORDS] = {0, };
	
	foreach_in_connected_groups(b, color, stones->move[0], stones_all_connected_handler, connected);

	for (int i = 0; i < stones->moves; i++)
		if (!connected[stones->move[i]])
			return false;
	return true;
}

/* min area size for living group (corner)
 * could increase to 10 (side) and 12 (middle)
 * and/or check prisoners */
#define NAKADE_MAX 8

/* Try to detect big eye area, ie:
 *  - completely enclosed area, not too big,
 *  - surrounding stones all connected to each other
 *  - size >= 2  (so no false eye issues)
 * Returns size of the area, or 0 if doesn't match.  */
int
big_eye_area(board_t *b, enum stone color, coord_t around, mq_t *area)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(around));
	assert(board_at(b, around) == S_NONE);
#endif
	mq_init(area);
	mq_t border;  mq_init(&border);

	mq_add(area, around);

	for (int i = 0; i < area->moves; i++) {
		foreach_neighbor(b, area->move[i], {
			if (board_at(b, c) == S_OFFBOARD)
				continue;
			
			if (board_at(b, c) == color) {	// Found border, save it and continue
				mq_add_nodup(&border, c);
				continue;
			}

			// Empty spot or prisoner, add it to area
			mq_add_nodup(area, c);
			if (area->moves > NAKADE_MAX)
				return 0;
		});
	}

	if (area->moves < 3 || !stones_all_connected(b, color, &border))
		return 0;
	
	return area->moves;
}


/* Point we control: 
 * Opponent can't play there or we can capture if he does.
 * TODO - handle more exotic cases (ladders ?) */
bool
is_controlled_eye_point(board_t *b, coord_t to, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
	assert(no_stone_at(to));
#endif
	enum stone other_color = stone_other(color);

	/* Eye-like ? */
	if (!board_is_valid_play_no_suicide(b, other_color, to))
		return true;

	/* Tiger mouth / selfatari ? */
	if (is_selfatari(b, other_color, to))
		return true;

	/* Can capture ? */
	with_move(b, to, other_color, {
		group_t g = group_at(b, to);
		if (!g || group_libs(b, g) != 2)
			break;
		for (int i = 0; i < 2; i++)
			if (wouldbe_ladder_any(b, g, group_lib(b, g, i)))
				with_move_return(true);
	});

	return false;
}


static bool
real_eye_endpoint(board_t *b, coord_t to, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
#endif
	enum stone other_color = stone_other(color);
	int color_diag_libs[S_MAX] = { 0, };

	foreach_diag_neighbor(b, to, {
		if (board_at(b, c) == other_color &&
		    group_libs(b, group_at(b, c)) == 1 &&
		    is_ladder_any(b, group_at(b, c), true)) {  /* Prisoner */
			color_diag_libs[color]++;
			continue;
		}

		if (board_at(b, c) == S_NONE &&
		    is_controlled_eye_point(b, c, color)) {   /* No need to recurse, thank goodness */
			color_diag_libs[color]++;
			continue;
		}

		color_diag_libs[(enum stone) board_at(b, c)]++;
	});

	/* We need to control 3 corners of the eye in the middle of the board,
	 * 2 on the side, and 1 in the corner. */
	if (color_diag_libs[S_OFFBOARD])
		color_diag_libs[color] += color_diag_libs[S_OFFBOARD] - 1;

	return (color_diag_libs[color] >= 3);
}

/* Point is finished one point eye.
 * (board_is_one_point_eye() ones can become false later ...) */
static bool
is_real_one_point_eye(board_t *b, coord_t to, enum stone color)
{
	return (board_is_eyelike(b, to, color) &&
		real_eye_endpoint(b, to, color));
}

static bool
is_real_two_point_eye(board_t *b, coord_t to, enum stone color, coord_t *pother)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(is_player_color(color));
	assert(board_at(b, to) == S_NONE);
#endif
	if ((neighbor_count_at(b, to, color) +
	     neighbor_count_at(b, to, S_OFFBOARD)) != 3)
		return false;
	coord_t other = pass;
	foreach_neighbor(b, to, {	/* Find the other point ... */
		if ((board_at(b, c) == S_NONE ||
		     board_at(b, c) == stone_other(color)) &&
		    (neighbor_count_at(b, c, color) +	    
		     neighbor_count_at(b, c, S_OFFBOARD)) == 3) {
			other = c;
			break;
		}
	});
	*pother = other;
	
	return (!is_pass(other) && 
		real_eye_endpoint(b, to, color) && 
		real_eye_endpoint(b, other, color));
}

typedef struct {
	int *visited;
	int  eyes;
} safe_data_t;

static int
count_eyes(board_t *b, enum stone color, coord_t lib, void *data)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(lib));
	assert(board_at(b, lib) == S_NONE);
#endif
	safe_data_t *d = (safe_data_t*)data;
	if (d->visited[lib])  /* Don't visit big eyes multiple times */
		return 0;

	if (is_real_one_point_eye(b, lib, color))  {
		// fprintf(stderr, "real eye: %s\n", coord2sstr(lib, b));
		if (++(d->eyes) >= 2)
			return -1;
		return 0;
	}
		
	coord_t other = pass;
	if (is_real_two_point_eye(b, lib, color, &other))  {
		// fprintf(stderr, "two-point eye: %s\n", coord2sstr(lib, b));
		d->visited[other] = 1;
		if (++(d->eyes) >= 2)
			return -1;
		return 0;
	}

	mq_t area;
	int area_size = big_eye_area(b, color, lib, &area);
	if (area_size) {
		/* Mark area visited */
		for (int i = 0; i < area.moves; i++)
			d->visited[area.move[i]] = 1;

		/* Check nakade area shape
		 *    O O O O    XXX we look at empty eyespace (prisoners removed)
		 *    O X . O        so would count this as one eye...
		 *    O O X O
		 *    . O O O    */
		int single_eye = nakade_area_dead_shape(b, &area);
		
		d->eyes += (single_eye ? 1 : 2);
		if (d->eyes >= 2)
			return -1;
		return 0;
	}
	
	return 0;
}

bool
dragon_is_safe(board_t *b, group_t g, enum stone color)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, g));
	assert(is_player_color(color));
	assert(board_at(b, g) == color);
#endif
	int visited[BOARD_MAX_COORDS] = {0, };
	safe_data_t d = { visited, 0 };
	foreach_lib_in_connected_groups(b, color, g, count_eyes, &d);
	return (d.eyes >= 2);
}

static inline bool
have_group_in(group_t g, group_t *groups, int ngroups)
{
	for (int i = 0; i < ngroups; i++)
		if (groups[i] == g) 
			return true;
	return false;
}

static int
group_neighbors(board_t *b, coord_t to, group_t *neighbors)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
	assert(group_at(b, to));
#endif
	group_t group = group_at(b, to);    assert(group);
	enum stone color = board_at(b, to);
	enum stone other_color = stone_other(color);
	
	int n = 0;       
	foreach_in_group(b, group) {
		foreach_neighbor(b, c, {
			if (board_at(b, c) != other_color)
				continue;
			group_t g = group_at(b, c);
			if (have_group_in(g, neighbors, n))
				continue;
			neighbors[n++] = g;
		});		
	} foreach_in_group_end;
	return n;
}

/* At least one neighbor is safe */
bool
neighbor_is_safe(board_t *b, group_t g)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, g));
#endif
	group_t neighbors[BOARD_MAX_GROUPS];
	int n = group_neighbors(b, g, neighbors);
	for (int i = 0; i < n; i++)
		if (dragon_is_safe(b, neighbors[i], board_at(b, neighbors[i])))
			return true;
	return false;
}


static int
count_libs(board_t *b, enum stone color, coord_t c, void *data)
{	
	int *libs = (int*)data;
	(*libs)++;  return 0;
}

int
dragon_liberties(board_t *b, enum stone color, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(to));
	assert(board_at(b, to) == color);
#endif
	int libs = 0;	
	foreach_lib_in_connected_groups(b, color, to, count_libs, &libs);
	return libs;
}


static int
dragon_at_handler(board_t *b, enum stone color, group_t g, void *data)
{
	group_t *d = (group_t*)data;
	*d = (*d > g ? *d : g);  return 0;		
}

group_t
dragon_at(board_t *b, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(sane_coord(to));
#endif
	group_t g = group_at(b, to);
	if (!g)
		return 0;
	
	group_t d = 0;
	enum stone color = board_at(b, to);
	foreach_connected_group(b, color, to, dragon_at_handler, &d);
	return d;
}


#define		GAP_LENGTH        4

/* Vertical gap ? */
static inline bool
is_vert_gap(board_t *b, enum stone color, int *connected, int lx, int ly,    int x, int dy) 
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(dy);
#endif
	for (int i = 0; i < GAP_LENGTH; i++) {
		int y = ly + dy * i;		
		coord_t d = coord_xy(x, y);
#ifdef EXTRA_CHECKS
		assert(sane_coord(d));
#endif
		if (board_at(b, d) == S_NONE)
			continue;
		if (board_at(b, d) == color && !connected[d])
			return false; // reach other group, could still be cut though ...
		if (board_at(b, d) == color && connected[d])
			return false; // wrong direction
		return false;
	}
	//fprintf(stderr, "vert gap %s %s\n", (dy > 0 ? "above" : "below"), coord2sstr(coord_xy(b, x, ly), b));
	return true;
}

/* Horizontal gap ? */
static inline bool
is_horiz_gap(board_t *b, enum stone color, int *connected, int lx, int ly,   int y, int dx)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(dx);
#endif
	for (int i = 0; i < GAP_LENGTH; i++) {
		int x = lx + dx * i;
		coord_t d = coord_xy(x, y);
#ifdef EXTRA_CHECKS
		assert(sane_coord(d));
#endif
		if (board_at(b, d) == S_NONE)
			continue;
		if (board_at(b, d) == color && !connected[d])
			return false; // reach other group, could still be cut though ...
		if (board_at(b, d) == color && connected[d])
			return false; // wrong direction
		return false;
	}
	return true;
}

#define vert_gap(x, dy)  is_vert_gap(b,  color, connected, lx, ly,   x, dy)
#define horiz_gap(y, dx) is_horiz_gap(b, color, connected, lx, ly,   y, dx)

/* Looking for 2-stones horizontal/vertical gap of length GAP_LENGTH extending
 * outwards from lib. For example, something like this:
 *
 *    . X X . . .        X X X X X . . .
 *    . O . X . .        X O O O * * * *
 *    . O * * * *        X O . O * * * *
 *    . O * * * *        X . O . . . . .
 *    . O . X . .      
 */
static bool
two_stones_gap(board_t *b, enum stone color, coord_t lib, int *connected) 
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(lib));
	assert(board_at(b, lib) == S_NONE);
#endif
	int lx = coord_x(lib);
	int ly = coord_y(lib);

	for (int dx = -1; dx <= 1; dx++)
		for (int dy = -1; dy <= 1; dy++) {
			if (dy && !dx) 
				if ( vert_gap(lx, dy) &&  // center gap + 1 on either side
				     (vert_gap(lx - 1, dy) || vert_gap(lx + 1, dy)) )
					return true;			
			if (dx && !dy) 
				if ( horiz_gap(ly, dx) &&  // center gap + 1 on either side
				     (horiz_gap(ly - 1, dx) || horiz_gap(ly + 1, dx)) ) 
					return true;	
		}
	return false;
}

static int
mark_connected(board_t *b,  enum stone color, coord_t c, void *data)
{
	int *connected = (int*)data;
	connected[c] = 1;  return 0;
}

typedef struct {
	int *connected;
	bool surrounded;
} surrounded_data_t;

static int 
surrounded_check(board_t *b,  enum stone color, coord_t lib, void *data)
{
#ifdef EXTRA_CHECKS
	assert(is_player_color(color));
	assert(sane_coord(lib));
	assert(board_at(b, lib) == S_NONE);
#endif
	surrounded_data_t *d = (surrounded_data_t*)data;
	if (two_stones_gap(b, color, lib, d->connected)) {	
		d->surrounded = 0;    return -1;   
	}
	/* Other group we could connect to ? */
	foreach_neighbor(b, lib, {
		if (board_at(b, c) == color && !d->connected[c]) {
			with_move(b, lib, color, {
				if (!group_at(b, lib))
					break;
				d->surrounded = dragon_is_surrounded(b, lib);
				with_move_return(-1);
			});
		}
	});
	return 0;
}

// XXX change coord_t -> group_t ?
bool
dragon_is_surrounded(board_t *b, coord_t to)
{
#ifdef EXTRA_CHECKS
	assert(sane_group(b, group_at(b, to)));
#endif
	enum stone color = board_at(b, to);
	assert(color == S_BLACK || color == S_WHITE);
	int connected[BOARD_MAX_COORDS] = {0, };

	/* Mark connected stones */
	foreach_in_connected_groups(b, color, to, mark_connected, connected);
	
	surrounded_data_t d = { connected, 1 };
	foreach_lib_in_connected_groups(b, color, to, surrounded_check, &d);
	return d.surrounded;
}

