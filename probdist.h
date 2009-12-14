#ifndef ZZGO_PROBDIST_H
#define ZZGO_PROBDIST_H

/* Tools for picking a move according to a probability distribution. */

#include "move.h"

struct probdist {
	int bsize2;
	float *moves; // [bsize2]
	float total;
};

struct probdist *probdist_init(struct probdist *pd, int bsize2);
static void probdist_add(struct probdist *pd, coord_t c, float val);
static void probdist_mul(struct probdist *pd, coord_t c, float val);
static void probdist_punch(struct probdist *pd, coord_t c); // Remove c from probability distribution
coord_t probdist_pick(struct probdist *pd);
void probdist_done(struct probdist *pd); // Doesn't free pd itself


static inline void
probdist_add(struct probdist *pd, coord_t c, float val)
{
	assert(c >= 0 && c < pd->bsize2);
	assert(val >= 0);
	pd->moves[c] += val;
	pd->total += val;
}

static inline void
probdist_mul(struct probdist *pd, coord_t c, float val)
{
	assert(c >= 0 && c < pd->bsize2);
	assert(val >= 0);
	pd->total += (val - 1) * pd->moves[c];
	pd->moves[c] *= val;
}

static inline void
probdist_punch(struct probdist *pd, coord_t c)
{
	assert(c >= 0 && c < pd->bsize2);
	pd->total -= pd->moves[c];
	pd->moves[c] = 0;
}

#endif
