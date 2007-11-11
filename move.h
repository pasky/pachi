#ifndef ZZGO_MOVE_H
#define ZZGO_MOVE_H

#include "stone.h"

struct coord {
	int x, y;
};

static struct coord pass = { -1, -1 };
static struct coord resign = { -2, -2 };
#define is_pass(c) ((c).x == pass.x && (c).y == pass.y)
#define is_resign(c) ((c).x == resign.x && (c).y == resign.y)

/* dyn allocated */
static struct coord *coord_init(int x, int y);
static struct coord *coord_copy(struct coord c);
static struct coord *coord_pass(void);
static struct coord *coord_resign(void);
static void coord_done(struct coord *c);

char *coord2str(struct coord c);
struct coord *str2coord(char *str);


struct move {
	struct coord coord;
	enum stone color;
};



static inline struct coord *
coord_init(int x, int y)
{
	struct coord *c = calloc(1, sizeof(struct coord));
	c->x = x; c->y = y;
	return c;
}

static inline struct coord *
coord_copy(struct coord c)
{
	return coord_init(c.x, c.y);
}

static inline struct coord *
coord_pass()
{
	return coord_copy(pass);
}

static inline struct coord *
coord_resign()
{
	return coord_copy(resign);
}

static inline void
coord_done(struct coord *c)
{
	free(c);
}

#endif
