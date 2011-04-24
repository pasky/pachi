#ifndef PACHI_PROBDIST_H
#define PACHI_PROBDIST_H

/* Tools for picking an item according to a probability distribution. */

/* The probability distribution structure is designed to be once
 * initialized, then random items assigned a value repeatedly and
 * random items picked repeatedly as well. */

#include "fixp.h"
#include "move.h"
#include "util.h"

struct board;

/* The interface looks a bit funny-wrapped since we used to switch
 * between different probdist representations. */

struct probdist {
	struct board *b;
	fixp_t *items; // [bsize2], [i] = P(pick==i)
	fixp_t *rowtotals; // [bsize], [i] = sum of items in row i
	fixp_t total; // sum of all items
};


/* Declare pd_ corresponding to board b_ in the local scope. */
#define probdist_alloca(pd_, b_) \
	fixp_t pd_ ## __pdi[board_size2(b_)] __attribute__((aligned(32))); memset(pd_ ## __pdi, 0, sizeof(pd_ ## __pdi)); \
	fixp_t pd_ ## __pdr[board_size(b_)] __attribute__((aligned(32))); memset(pd_ ## __pdr, 0, sizeof(pd_ ## __pdr)); \
	struct probdist pd_ = { .b = b_, .items = pd_ ## __pdi, .rowtotals = pd_ ## __pdr, .total = 0 };

/* Get the value of given item. */
#define probdist_one(pd, c) ((pd)->items[c])

/* Get the cummulative probability value (normalizing constant). */
#define probdist_total(pd) ((pd)->total)

/* Set the value of given item. */
static void probdist_set(struct probdist *pd, coord_t c, fixp_t val);

/* Remove the item from the totals; this is used when you then
 * pass it in the ignore list to probdist_pick(). Of course you
 * must restore the totals afterwards. */
static void probdist_mute(struct probdist *pd, coord_t c);

/* Pick a random item. ignore is a pass-terminated sorted array of items
 * that are not to be considered (and whose values are not in @total). */
coord_t probdist_pick(struct probdist *pd, coord_t *ignore);


/* Now, we do something horrible - include board.h for the inline helpers.
 * Yay for us. */
#include "board.h"


static inline void
probdist_set(struct probdist *pd, coord_t c, fixp_t val)
{
	/* We disable the assertions here since this is quite time-critical
	 * part of code, and also the compiler is reluctant to inline the
	 * functions otherwise. */
#if 0
	assert(c >= 0 && c < board_size2(pd->b));
	assert(val >= 0);
#endif
	pd->total += val - pd->items[c];
	pd->rowtotals[coord_y(c, pd->b)] += val - pd->items[c];
	pd->items[c] = val;
}

static inline void
probdist_mute(struct probdist *pd, coord_t c)
{
	pd->total -= pd->items[c];
	pd->rowtotals[coord_y(c, pd->b)] -= pd->items[c];
}

#endif
