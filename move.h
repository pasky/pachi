#ifndef ZZGO_MOVE_H
#define ZZGO_MOVE_H

#include "stone.h"

typedef struct coord {
	int x, y;
} coord_t;

static coord_t pass = { -1, -1 };
static coord_t resign = { -2, -2 };
#define is_pass(c) ((c).x == pass.x && (c).y == pass.y)
#define is_resign(c) ((c).x == resign.x && (c).y == resign.y)

/* dyn allocated */
static coord_t *coord_init(int x, int y);
static coord_t *coord_copy(coord_t c);
static coord_t *coord_pass(void);
static coord_t *coord_resign(void);
static void coord_done(coord_t *c);

char *coord2str(coord_t c);
coord_t *str2coord(char *str);


struct move {
	coord_t coord;
	enum stone color;
};



static inline coord_t *
coord_init(int x, int y)
{
	coord_t *c = calloc(1, sizeof(coord_t));
	c->x = x; c->y = y;
	return c;
}

static inline coord_t *
coord_copy(coord_t c)
{
	return coord_init(c.x, c.y);
}

static inline coord_t *
coord_pass()
{
	return coord_copy(pass);
}

static inline coord_t *
coord_resign()
{
	return coord_copy(resign);
}

static inline void
coord_done(coord_t *c)
{
	free(c);
}

#endif
