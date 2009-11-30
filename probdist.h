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
void probdist_add(struct probdist *pd, coord_t c, float val);
void probdist_punch(struct probdist *pd, coord_t c); // Remove c from probability distribution
coord_t probdist_pick(struct probdist *pd);
void probdist_done(struct probdist *pd); // Doesn't free pd itself

#endif
